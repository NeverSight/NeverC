//===----------------------------------------------------------------------===//
//
//  Stopwatch — scoped hierarchical timer tree.
//
//  Each backend creates a root `Timer` for its `link()` invocation and
//  attaches child timers for coarse-grained phases (parse, resolve,
//  write, ...).  `ScopedTimer` accumulates the elapsed nanoseconds into
//  its parent `Timer` when it goes out of scope; `Timer::print()`
//  renders the tree on a `message()` stream when `--time-trace` (or a
//  similar flag) is requested.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Runtime/Stopwatch.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Format.h"
#include <ratio>

using namespace linker;
using namespace llvm;

ScopedTimer::ScopedTimer(Timer &t) : t(&t) {
  startTime = std::chrono::high_resolution_clock::now();
}

void ScopedTimer::stop() {
  if (!t)
    return;
  t->addToTotal(std::chrono::high_resolution_clock::now() - startTime);
  t = nullptr;
}

ScopedTimer::~ScopedTimer() { stop(); }

Timer::Timer(llvm::StringRef name) : total(0), name(std::string(name)) {}
Timer::Timer(llvm::StringRef name, Timer &parent)
    : total(0), name(std::string(name)) {
  parent.children.push_back(this);
}

void Timer::print() {
  double totalDuration = static_cast<double>(millis());

  // Render the children first, with the grand-total printed underneath
  // on its own line so the tree reads top-to-bottom.
  for (const auto &child : children)
    if (child->total > 0)
      child->print(1, totalDuration);

  message(std::string(50, '-'));

  print(0, millis(), false);
}

double Timer::millis() const {
  return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
             std::chrono::nanoseconds(total))
      .count();
}

void Timer::print(int depth, double totalDuration, bool recurse) const {
  double p = 100.0 * millis() / totalDuration;

  SmallString<32> str;
  llvm::raw_svector_ostream stream(str);
  std::string s = std::string(depth * 2, ' ') + name + std::string(":");
  stream << format("%-30s%7d ms (%5.1f%%)", s.c_str(), (int)millis(), p);

  message(str);

  if (recurse) {
    for (const auto &child : children)
      if (child->total > 0)
        child->print(depth + 1, totalDuration);
  }
}
