//===-- llvm/ADT/Statistic.h - Easy way to expose stats ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the 'Statistic' class, which is designed to be an easy way
/// to expose various metrics from passes.  These statistics are printed at the
/// end of a run (from llvm_shutdown), when the -stats command line option is
/// passed on the command line.
///
/// This is useful for reporting information like the number of instructions
/// simplified, optimized or removed by various transformations, like this:
///
/// static Statistic NumInstsKilled("gcse", "Number of instructions killed");
///
/// Later, in the code: ++NumInstsKilled;
///
/// NOTE: Statistics *must* be declared as global variables.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STATISTIC_H
#define LLVM_ADT_STATISTIC_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Timer.h"
#include <algorithm>
#include <atomic>
#include <memory>

// Determine whether statistics should be enabled. We must do it here rather
// than in CMake because multi-config generators cannot determine this at
// configure time.
#if !defined(NDEBUG) || LLVM_FORCE_ENABLE_STATS
#define LLVM_ENABLE_STATS 1
#else
#define LLVM_ENABLE_STATS 0
#endif

namespace llvm {

class raw_ostream;
class raw_fd_ostream;
class StringRef;

class TrackingStatistic {
public:
  const char *const DebugType;
  const char *const Name;
  const char *const Desc;

  std::atomic<uint64_t> Value;
  std::atomic<bool> Initialized;

  constexpr TrackingStatistic(const char *DebugType, const char *Name,
                              const char *Desc)
      : DebugType(DebugType), Name(Name), Desc(Desc), Value(0),
        Initialized(false) {}

  const char *getDebugType() const { return DebugType; }
  const char *getName() const { return Name; }
  const char *getDesc() const { return Desc; }

  uint64_t getValue() const { return Value.load(std::memory_order_relaxed); }

  // Allow use of this class as the value itself.
  operator uint64_t() const { return getValue(); }

  const TrackingStatistic &operator=(uint64_t Val) {
    Value.store(Val, std::memory_order_relaxed);
    return init();
  }

  const TrackingStatistic &operator++() {
    Value.fetch_add(1, std::memory_order_relaxed);
    return init();
  }

  uint64_t operator++(int) {
    init();
    return Value.fetch_add(1, std::memory_order_relaxed);
  }

  const TrackingStatistic &operator--() {
    Value.fetch_sub(1, std::memory_order_relaxed);
    return init();
  }

  uint64_t operator--(int) {
    init();
    return Value.fetch_sub(1, std::memory_order_relaxed);
  }

  const TrackingStatistic &operator+=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_add(V, std::memory_order_relaxed);
    return init();
  }

  const TrackingStatistic &operator-=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_sub(V, std::memory_order_relaxed);
    return init();
  }

  void updateMax(uint64_t V) {
    uint64_t PrevMax = Value.load(std::memory_order_relaxed);
    // Keep trying to update max until we succeed or another thread produces
    // a bigger max than us.
    while (V > PrevMax && !Value.compare_exchange_weak(
                              PrevMax, V, std::memory_order_relaxed)) {
    }
    init();
  }

protected:
  TrackingStatistic &init() {
    if (!Initialized.load(std::memory_order_acquire))
      RegisterStatistic();
    return *this;
  }

  void RegisterStatistic();
};

class NoopStatistic {
public:
  NoopStatistic(const char * /*DebugType*/, const char * /*Name*/,
                const char * /*Desc*/) {}

  uint64_t getValue() const { return 0; }

  // Allow use of this class as the value itself.
  operator uint64_t() const { return 0; }

  const NoopStatistic &operator=(uint64_t Val) { return *this; }

  const NoopStatistic &operator++() { return *this; }

  uint64_t operator++(int) { return 0; }

  const NoopStatistic &operator--() { return *this; }

  uint64_t operator--(int) { return 0; }

  const NoopStatistic &operator+=(const uint64_t &V) { return *this; }

  const NoopStatistic &operator-=(const uint64_t &V) { return *this; }

  void updateMax(uint64_t V) {}
};

#if LLVM_ENABLE_STATS
using Statistic = TrackingStatistic;
#else
using Statistic = NoopStatistic;
#endif

// STATISTIC - A macro to make definition of statistics really simple.  This
// automatically passes the DEBUG_TYPE of the file into the statistic.
#define STATISTIC(VARNAME, DESC)                                               \
  static llvm::Statistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}

// ALWAYS_ENABLED_STATISTIC - A macro to define a statistic like STATISTIC but
// it is enabled even if LLVM_ENABLE_STATS is off.
#define ALWAYS_ENABLED_STATISTIC(VARNAME, DESC)                                \
  static llvm::TrackingStatistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}

/// Enable the collection and printing of statistics.
void EnableStatistics(bool DoPrintOnExit = true);

/// Check if statistics are enabled.
bool AreStatisticsEnabled();

/// Return a file stream to print our output on.
std::unique_ptr<raw_fd_ostream> CreateInfoOutputFile();

/// Print statistics to the file returned by CreateInfoOutputFile().
void PrintStatistics();

/// Print statistics to the given output stream.
void PrintStatistics(raw_ostream &OS);

/// Print statistics in JSON format. This does include all global timers (\see
/// Timer, TimerGroup). Note that the timers are cleared after printing and will
/// not be printed in human readable form or in a second call of
/// PrintStatisticsJSON().
void PrintStatisticsJSON(raw_ostream &OS);

/// Get the statistics. This can be used to look up the value of
/// statistics without needing to parse JSON.
///
/// This function does not prevent statistics being updated by other threads
/// during it's execution. It will return the value at the point that it is
/// read. However, it will prevent new statistics from registering until it
/// completes.
struct StatisticEntry {
  StringRef Name;
  uint64_t Value;
};
SmallVector<StatisticEntry, 0> GetStatistics();

/// Reset the statistics. This can be used to zero and de-register the
/// statistics in order to measure a compilation.
///
/// When this function begins to call destructors prior to returning, all
/// statistics will be zero and unregistered. However, that might not remain the
/// case by the time this function finishes returning. Whether update from other
/// threads are lost or merely deferred until during the function return is
/// timing sensitive.
///
/// Callers who intend to use this to measure statistics for a single
/// compilation should ensure that no compilations are in progress at the point
/// this function is called and that only one compilation executes until calling
/// GetStatistics().
void ResetStatistics();

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

/// -stats - Command line option to cause transformations to emit stats about
/// what they did.
///
inline static bool EnableStats;
inline static bool StatsAsJSON;
inline static bool Enabled;
inline static bool PrintOnExit;

inline void initStatisticOptions() {
  static cl::opt<bool, true> registerEnableStats{
      "stats",
      cl::desc(
          "Enable statistics output from program (available with Asserts)"),
      cl::location(EnableStats), cl::Hidden};
  static cl::opt<bool, true> registerStatsAsJson{
      "stats-json", cl::desc("Display statistics as json data"),
      cl::location(StatsAsJSON), cl::Hidden};
}

namespace {
/// This class is used in a ManagedStatic so that it is created on demand (when
/// the first statistic is bumped) and destroyed only when llvm_shutdown is
/// called. We print statistics from the destructor.
/// This class is also used to look up statistic values from applications that
/// use LLVM.
class StatisticInfo {
  SmallVector<TrackingStatistic *, 64> Stats;

public:
  void sort();
  using const_iterator = SmallVector<TrackingStatistic *, 64>::const_iterator;

  StatisticInfo();
  ~StatisticInfo();

  void addStatistic(TrackingStatistic *S) { Stats.push_back(S); }

  const_iterator begin() const { return Stats.begin(); }
  const_iterator end() const { return Stats.end(); }
  iterator_range<const_iterator> statistics() const { return {begin(), end()}; }
  bool empty() const { return Stats.empty(); }

  void reset();
};
} // end anonymous namespace

inline static ManagedStatic<StatisticInfo> StatInfo;
inline static ManagedStatic<sys::SmartMutex<true>> StatLock;

/// RegisterStatistic - The first time a statistic is bumped, this method is
/// called.
inline void TrackingStatistic::RegisterStatistic() {
  // If stats are enabled, inform StatInfo that this statistic should be
  // printed.
  // llvm_shutdown calls destructors while holding the ManagedStatic mutex.
  // These destructors end up calling PrintStatistics, which takes StatLock.
  // Since dereferencing StatInfo and StatLock can require taking the
  // ManagedStatic mutex, doing so with StatLock held would lead to a lock
  // order inversion. To avoid that, we dereference the ManagedStatics first,
  // and only take StatLock afterwards.
  if (!Initialized.load(MO_RELAXED)) {
    sys::SmartMutex<true> &Lock = *StatLock;
    StatisticInfo &SI = *StatInfo;
    sys::SmartScopedLock<true> Writer(Lock);
    if (Initialized.load(MO_RELAXED))
      return;
    if (EnableStats || Enabled)
      SI.addStatistic(this);

    Initialized.store(true, MO_RELEASE);
  }
}

inline StatisticInfo::StatisticInfo() {
  // Ensure that necessary timer global objects are created first so they are
  // destructed after us.
  TimerGroup::constructForStatistics();
}

// Print information when destroyed, iff command line option is specified.
inline StatisticInfo::~StatisticInfo() {
  if (EnableStats || PrintOnExit)
    PrintStatistics();
}

inline void EnableStatistics(bool DoPrintOnExit) {
  Enabled = true;
  PrintOnExit = DoPrintOnExit;
}

inline bool AreStatisticsEnabled() { return Enabled || EnableStats; }

inline void StatisticInfo::sort() {
  stable_sort(Stats,
              [](const TrackingStatistic *LHS, const TrackingStatistic *RHS) {
                if (int Cmp = strcmp(LHS->getDebugType(), RHS->getDebugType()))
                  return Cmp < 0;

                if (int Cmp = strcmp(LHS->getName(), RHS->getName()))
                  return Cmp < 0;

                return strcmp(LHS->getDesc(), RHS->getDesc()) < 0;
              });
}

inline void StatisticInfo::reset() {
  sys::SmartScopedLock<true> Writer(*StatLock);

  // Tell each statistic that it isn't registered so it has to register
  // again. We're holding the lock so it won't be able to do so until we're
  // finished. Once we've forced it to re-register (after we return), then zero
  // the value.
  for (auto *Stat : Stats) {
    // Value updates to a statistic that complete before this statement in the
    // iteration for that statistic will be lost as intended.
    Stat->Initialized = false;
    Stat->Value = 0;
  }

  // Clear the registration list and release the lock once we're done. Any
  // pending updates from other threads will safely take effect after we return.
  // That might not be what the user wants if they're measuring a compilation
  // but it's their responsibility to prevent concurrent compilations to make
  // a single compilation measurable.
  Stats.clear();
}

inline void PrintStatistics(raw_ostream &OS) {
  StatisticInfo &SI = *StatInfo;

  // Figure out how long the biggest Value and Name fields are.
  unsigned MaxDebugTypeLen = 0, MaxValLen = 0;
  for (TrackingStatistic *Stat : SI.statistics()) {
    MaxValLen = std::max(MaxValLen, (unsigned)utostr(Stat->getValue()).size());
    MaxDebugTypeLen =
        std::max(MaxDebugTypeLen, (unsigned)strlen(Stat->getDebugType()));
  }

  SI.sort();

  char dashes[74];
  memset(dashes, '-', 73);
  dashes[73] = '\0';
  OS << "===" << dashes << "===\n"
     << "                          ... Statistics Collected ...\n"
     << "===" << dashes << "===\n\n";

  // Print all of the statistics.
  for (TrackingStatistic *Stat : SI.statistics())
    OS << format("%*" PRIu64 " %-*s - %s\n", MaxValLen, Stat->getValue(),
                 MaxDebugTypeLen, Stat->getDebugType(), Stat->getDesc());

  OS << '\n'; // Flush the output stream.
  OS.flush();
}

inline void PrintStatisticsJSON(raw_ostream &OS) {
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &SI = *StatInfo;

  SI.sort();

  // Print all of the statistics.
  OS << "{\n";
  const char *delim = "";
  for (const TrackingStatistic *Stat : SI.statistics()) {
    OS << delim;
    assert(yaml::needsQuotes(Stat->getDebugType()) == yaml::QuotingType::None &&
           "Statistic group/type name is simple.");
    assert(yaml::needsQuotes(Stat->getName()) == yaml::QuotingType::None &&
           "Statistic name is simple");
    OS << "\t\"" << Stat->getDebugType() << '.' << Stat->getName()
       << "\": " << Stat->getValue();
    delim = ",\n";
  }
  // Print timers.
  TimerGroup::printAllJSONValues(OS, delim);

  OS << "\n}\n";
  OS.flush();
}

inline void PrintStatistics() {
#if LLVM_ENABLE_STATS
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &SI = *StatInfo;

  // Statistics not enabled?
  if (SI.empty())
    return;

  // Get the stream to write to.
  uptr_t<raw_ostream> OutStream = CreateInfoOutputFile();
  if (StatsAsJSON)
    PrintStatisticsJSON(*OutStream);
  else
    PrintStatistics(*OutStream);

#else
  // Check if the -stats option is set instead of checking
  // !Stats.Stats.empty().  In release builds, Statistics operators
  // do nothing, so stats are never Registered.
  if (EnableStats) {
    // Get the stream to write to.
    uptr_t<raw_ostream> OutStream = CreateInfoOutputFile();
    (*OutStream) << "Statistics are disabled.  "
                 << "Build with asserts or with -DLLVM_FORCE_ENABLE_STATS\n";
  }
#endif
}

inline SmallVector<StatisticEntry, 0> GetStatistics() {
  sys::SmartScopedLock<true> Reader(*StatLock);
  SmallVector<StatisticEntry, 0> ReturnStats;

  for (const auto &Stat : StatInfo->statistics())
    ReturnStats.push_back({Stat->getName(), Stat->getValue()});
  return ReturnStats;
}

inline void ResetStatistics() { StatInfo->reset(); }

} // namespace llvm

#endif // LLVM_ADT_STATISTIC_H
