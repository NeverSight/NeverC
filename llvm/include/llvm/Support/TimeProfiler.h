//===- llvm/Support/TimeProfiler.h - Hierarchical Time Profiler -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides lightweight and dependency-free machinery to trace execution
// time around arbitrary code. Two API flavors are available.
//
// The primary API uses a RAII object to trigger tracing:
//
// \code
//   {
//     TimeTraceScope scope("my_event_name");
//     ...my code...
//   }
// \endcode
//
// If the code to be profiled does not have a natural lexical scope then
// it is also possible to start and end events with respect to an implicit
// per-thread stack of profiling entries:
//
// \code
//   timeTraceProfilerBegin("my_event_name");
//   ...my code...
//   timeTraceProfilerEnd();  // must be called on all control flow paths
// \endcode
//
// Time profiling entries can be given an arbitrary name and, optionally,
// an arbitrary 'detail' string. The resulting trace will include 'Total'
// entries summing the time spent for each name. Thus, it's best to choose
// names to be fairly generic, and rely on the detail field to capture
// everything else of interest.
//
// To avoid lifetime issues name and detail strings are copied into the event
// entries at their time of creation. Care should be taken to make string
// construction cheap to prevent 'Heisenperf' effects. In particular, the
// 'detail' argument may be a string-returning closure:
//
// \code
//   int n;
//   {
//     TimeTraceScope scope("my_event_name",
//                          [n]() { return (Twine("x=") + Twine(n)).str(); });
//     ...my code...
//   }
// \endcode
// The closure will not be called if tracing is disabled. Otherwise, the
// resulting string will be directly moved into the entry.
//
// The main process should begin with a timeTraceProfilerInitialize, and
// finish with timeTraceProfileWrite and timeTraceProfilerCleanup calls.
// Each new thread should begin with a timeTraceProfilerInitialize, and
// finish with a timeTraceProfilerFinishThread call.
//
// Timestamps come from std::chrono::stable_clock. Note that threads need
// not see the same time from that clock, and the resolution may not be
// the best available.
//
// Currently, there are a number of compatible viewers:
//  - chrome://tracing is the original chromium trace viewer.
//  - http://ui.perfetto.dev is the replacement for the above, under active
//    development by Google as part of the 'Perfetto' project.
//  - https://www.speedscope.app/ has also been reported as an option.
//
// Future work:
//  - Support akin to LLVM_DEBUG for runtime enable/disable of named tracing
//    families for non-debug builds which wish to support optional tracing.
//  - Evaluate the detail closures at profile write time to avoid
//    stringification costs interfering with tracing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TIMEPROFILER_H
#define LLVM_SUPPORT_TIMEPROFILER_H

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <cstdint>

namespace llvm {

class raw_pwrite_stream;

struct TimeTraceProfiler;
TimeTraceProfiler *getTimeTraceProfilerInstance();

/// Opaque identity for one explicitly managed root and its worker profilers.
/// Zero is reserved for the legacy process-wide API below.
using TimeTraceProfilerSession = uint64_t;

/// Return the session attached to the calling thread's profiler, or zero for
/// a legacy/raw profiler and for a thread without a profiler.
TimeTraceProfilerSession timeTraceProfilerCurrentSession();

/// Initialize an explicitly managed root. A nonzero session cannot start
/// while a foreign legacy profiler is active. Finished legacy profilers are
/// preserved and excluded from the managed trace.
Error timeTraceProfilerInitializeSession(unsigned TimeTraceGranularity,
                                         StringRef ProcName,
                                         TimeTraceProfilerSession Session);

/// Join an existing managed root from a worker thread. The session must still
/// be open and must not have begun its atomic write.
Error timeTraceProfilerInitializeThread(unsigned TimeTraceGranularity,
                                        StringRef ProcName,
                                        TimeTraceProfilerSession Session);

/// Atomically validate and serialize only one managed session. Returns an
/// Error instead of asserting when a root scope or worker is still active.
Error timeTraceProfilerWriteSession(raw_pwrite_stream &OS,
                                    TimeTraceProfilerSession Session);

/// Close and clean only one managed session. Active workers retain their TLS
/// profiler until they finish, at which point the closed session discards it.
void timeTraceProfilerCleanupSession(TimeTraceProfilerSession Session);

/// Delete only the profiler attached to the calling thread. This never sweeps
/// finished profilers belonging to another root or to the legacy API.
void timeTraceProfilerCleanupCurrentThread();

/// Initialize the time trace profiler.
/// This sets up the global \p TimeTraceProfilerInstance
/// variable to be the profiler instance.
void timeTraceProfilerInitialize(unsigned TimeTraceGranularity,
                                 StringRef ProcName);

/// Cleanup the time trace profiler, if it was initialized.
void timeTraceProfilerCleanup();

/// Finish a time trace profiler running on a worker thread.
void timeTraceProfilerFinishThread();

/// Is the time trace profiler enabled, i.e. initialized?
inline bool timeTraceProfilerEnabled() {
  return getTimeTraceProfilerInstance() != nullptr;
}

/// Write profiling data to output stream.
/// Data produced is JSON, in Chrome "Trace Event" format, see
/// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
void timeTraceProfilerWrite(raw_pwrite_stream &OS);

/// Write profiling data to a file.
/// The function will write to \p PreferredFileName if provided, if not
/// then will write to \p FallbackFileName appending .time-trace.
/// Returns an Error if the profiler generation is not ready or if the output
/// cannot be opened, written, flushed, or closed.
Error timeTraceProfilerWrite(StringRef PreferredFileName,
                             StringRef FallbackFileName);

/// Manually begin a time section, with the given \p Name and \p Detail.
/// Profiler copies the string data, so the pointers can be given into
/// temporaries. Time sections can be hierarchical; every Begin must have a
/// matching End pair but they can nest.
void timeTraceProfilerBegin(StringRef Name, StringRef Detail);
void timeTraceProfilerBegin(StringRef Name,
                            llvm::function_ref<SmallString<64>()> Detail);

/// Manually end the last time section.
void timeTraceProfilerEnd();

/// The TimeTraceScope is a helper class to call the begin and end functions
/// of the time trace profiler.  When the object is constructed, it begins
/// the section; and when it is destroyed, it stops it. If the time profiler
/// is not initialized, the overhead is a single branch.
struct TimeTraceScope {

  TimeTraceScope() = delete;
  TimeTraceScope(const TimeTraceScope &) = delete;
  TimeTraceScope &operator=(const TimeTraceScope &) = delete;
  TimeTraceScope(TimeTraceScope &&) = delete;
  TimeTraceScope &operator=(TimeTraceScope &&) = delete;

  TimeTraceScope(StringRef Name) {
    if (getTimeTraceProfilerInstance() != nullptr)
      timeTraceProfilerBegin(Name, StringRef(""));
  }
  TimeTraceScope(StringRef Name, StringRef Detail) {
    if (getTimeTraceProfilerInstance() != nullptr)
      timeTraceProfilerBegin(Name, Detail);
  }
  TimeTraceScope(StringRef Name, llvm::function_ref<SmallString<64>()> Detail) {
    if (getTimeTraceProfilerInstance() != nullptr)
      timeTraceProfilerBegin(Name, Detail);
  }
  ~TimeTraceScope() {
    if (getTimeTraceProfilerInstance() != nullptr)
      timeTraceProfilerEnd();
  }
};

} // end namespace llvm

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define LLVM_TP_MUTEX_T SRWLOCK
#define LLVM_TP_MUTEX_INITIALIZER SRWLOCK_INIT
#define LLVM_TP_MUTEX_LOCK(m) AcquireSRWLockExclusive(m)
#define LLVM_TP_MUTEX_UNLOCK(m) ReleaseSRWLockExclusive(m)
#else
#include <pthread.h>
#define LLVM_TP_MUTEX_T pthread_mutex_t
#define LLVM_TP_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define LLVM_TP_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define LLVM_TP_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif
#include <system_error>

// === Inline implementations (moved from cpp_bridge.cpp) ===

namespace llvm {

namespace {

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::steady_clock;
using std::chrono::system_clock;
using std::chrono::time_point;
using std::chrono::time_point_cast;
using microseconds = std::chrono::microseconds;

} // anonymous namespace

// Named for the same reason as TimeTraceProfilerInstance below: the registry
// of live profilers has to be one object for the whole program.  In an
// anonymous namespace each translation unit gets its own list, so a profiler
// registered by one would be missing from the write-out driven by another.
namespace time_trace_detail {

struct TimeTraceProfilerInstanceEntry {
  TimeTraceProfiler *Profiler = nullptr;
  TimeTraceProfilerSession Session = 0;
  uint64_t Epoch = 0;
};

struct TimeTraceProfilerSessionState {
  TimeTraceProfilerSession Session = 0;
  uint64_t Epoch = 0;
  size_t LiveInstances = 0;
  TimeTraceProfiler *Root = nullptr;
  bool Closing = false;
  bool Sealed = false;
};

struct TimeTraceProfilerInstances {
  LLVM_TP_MUTEX_T Lock = LLVM_TP_MUTEX_INITIALIZER;
  SmallVector<TimeTraceProfilerInstanceEntry, 8> List;
  SmallVector<TimeTraceProfilerSessionState, 4> Sessions;
  uint64_t NextLegacyEpoch = 1;
};

inline TimeTraceProfilerInstances &getTimeTraceProfilerInstances() {
  static TimeTraceProfilerInstances Instances;
  return Instances;
}

inline TimeTraceProfilerSessionState *
findSessionState(TimeTraceProfilerInstances &Instances,
                 TimeTraceProfilerSession Session, uint64_t Epoch) {
  auto It = llvm::find_if(Instances.Sessions, [=](const auto &State) {
    return State.Session == Session && State.Epoch == Epoch;
  });
  return It == Instances.Sessions.end() ? nullptr : &*It;
}

inline TimeTraceProfilerSessionState *
findOpenLegacyState(TimeTraceProfilerInstances &Instances) {
  auto It = llvm::find_if(Instances.Sessions, [](const auto &State) {
    return State.Session == 0 && !State.Closing && !State.Sealed;
  });
  return It == Instances.Sessions.end() ? nullptr : &*It;
}

inline size_t countFinished(TimeTraceProfilerInstances &Instances,
                            TimeTraceProfilerSession Session, uint64_t Epoch) {
  return llvm::count_if(Instances.List, [=](const auto &Entry) {
    return Entry.Session == Session && Entry.Epoch == Epoch;
  });
}

} // namespace time_trace_detail

// Per Thread instance — must NOT be `static` (internal linkage): the inline
// functions below have external linkage, so the linker deduplicates them
// across TUs; if the variable were static each TU would get its own copy
// and the surviving inline function could reference a different TU's copy
// than the one that was initialised.
inline LLVM_THREAD_LOCAL TimeTraceProfiler *TimeTraceProfilerInstance = 0;
inline LLVM_THREAD_LOCAL TimeTraceProfilerSession
    TimeTraceProfilerInstanceSession = 0;

inline TimeTraceProfiler *getTimeTraceProfilerInstance() {
  return TimeTraceProfilerInstance;
}

inline TimeTraceProfilerSession timeTraceProfilerCurrentSession() {
  return TimeTraceProfilerInstance ? TimeTraceProfilerInstanceSession : 0;
}

namespace {

using ClockType = steady_clock;
using TimePointType = time_point<ClockType>;
using DurationType = duration<ClockType::rep, ClockType::period>;
struct CountAndDurationType {
  size_t first;
  DurationType second;
  CountAndDurationType() : first(0), second(DurationType::zero()) {}
};
struct NameAndCountAndDurationType {
  SmallString<64> first;
  CountAndDurationType second;
  NameAndCountAndDurationType(StringRef N, CountAndDurationType C)
      : first(N), second(C) {}
};

struct TimeTraceProfilerEntry {
  const TimePointType Start;
  TimePointType End;
  const SmallString<64> Name;
  const SmallString<128> Detail;

  TimeTraceProfilerEntry(TimePointType S, TimePointType E, StringRef N,
                         StringRef Dt)
      : Start(S), End(E), Name(N), Detail(Dt) {}

  // Calculate timings for FlameGraph. Cast time points to microsecond precision
  // rather than casting duration. This avoids truncation issues causing inner
  // scopes overruning outer scopes.
  ClockType::rep getFlameGraphStartUs(TimePointType StartTime) const {
    return (time_point_cast<microseconds>(Start) -
            time_point_cast<microseconds>(StartTime))
        .count();
  }

  ClockType::rep getFlameGraphDurUs() const {
    return (time_point_cast<microseconds>(End) -
            time_point_cast<microseconds>(Start))
        .count();
  }
};

} // anonymous namespace

struct TimeTraceProfiler {
  TimeTraceProfiler(unsigned TimeTraceGranularity = 0, StringRef ProcName = "",
                    TimeTraceProfilerSession Session = 0,
                    uint64_t Epoch = 0)
      : BeginningOfTime(system_clock::now()), StartTime(ClockType::now()),
        ProcName(ProcName), Pid(sys::Process::getProcessId()),
        Tid(llvm::get_threadid()), TimeTraceGranularity(TimeTraceGranularity),
        Session(Session), Epoch(Epoch) {
    llvm::get_thread_name(ThreadName);
  }

  void begin(StringRef Name, llvm::function_ref<SmallString<64>()> Detail) {
    Stack.emplace_back(ClockType::now(), TimePointType(), Name,
                       StringRef(Detail()));
  }

  void end() {
    assert(!Stack.empty() && "Must call begin() first");
    TimeTraceProfilerEntry &E = Stack.back();
    E.End = ClockType::now();

    // Check that end times monotonically increase.
    assert((Entries.empty() ||
            (E.getFlameGraphStartUs(StartTime) + E.getFlameGraphDurUs() >=
             Entries.back().getFlameGraphStartUs(StartTime) +
                 Entries.back().getFlameGraphDurUs())) &&
           "TimeProfiler scope ended earlier than previous scope");

    // Calculate duration at full precision for overall counts.
    DurationType Duration = E.End - E.Start;

    // Only include sections longer or equal to TimeTraceGranularity msec.
    if (duration_cast<microseconds>(Duration).count() >= TimeTraceGranularity)
      Entries.emplace_back(E);

    // Track total time taken by each "name", but only the topmost levels of
    // them; e.g. if there's a template instantiation that instantiates other
    // templates from within, we only want to add the topmost one. "topmost"
    // happens to be the ones that don't have any currently open entries above
    // itself.
    if (llvm::none_of(llvm::drop_begin(llvm::reverse(Stack)),
                      [&](const TimeTraceProfilerEntry &Val) {
                        return Val.Name == E.Name;
                      })) {
      auto &CountAndTotal = CountAndTotalPerName[E.Name];
      CountAndTotal.first++;
      CountAndTotal.second += Duration;
    }

    Stack.pop_back();
  }

  // Validate and render one profiler generation while its registry snapshot
  // is stable. The caller writes the returned bytes only after this function
  // releases the registry lock, so a custom output stream may safely re-enter
  // profiler cleanup without deadlocking the non-recursive platform mutex.
  Expected<SmallVector<char, 0>>
  render(TimeTraceProfilerSession RequestedSession, uint64_t RequestedEpoch) {
    SmallVector<char, 0> Bytes;
    auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
    LLVM_TP_MUTEX_LOCK(&Instances.Lock);
    auto Unlock =
        llvm::make_scope_exit([&] { LLVM_TP_MUTEX_UNLOCK(&Instances.Lock); });
    auto Failure = [](const char *Message) -> Error {
      return createStringError(inconvertibleErrorCode(), Message);
    };

    if (Session != RequestedSession || Epoch != RequestedEpoch ||
        TimeTraceProfilerInstance != this ||
        TimeTraceProfilerInstanceSession != RequestedSession)
      return Failure("LLVM time-trace session ownership changed before write");
    auto *State = time_trace_detail::findSessionState(
        Instances, RequestedSession, RequestedEpoch);
    if (!State || State->Root != this || State->Closing)
      return Failure("LLVM time-trace session is no longer active");
    if (!Stack.empty())
      return Failure("LLVM time-trace root still has active scopes");

    const size_t FinishedCount = time_trace_detail::countFinished(
        Instances, RequestedSession, RequestedEpoch);
    if (State->LiveInstances != FinishedCount + 1)
      return Failure("LLVM time-trace session has active worker profilers");
    for (const auto &Entry : Instances.List) {
      if (Entry.Session == RequestedSession && Entry.Epoch == RequestedEpoch &&
          !Entry.Profiler->Stack.empty())
        return Failure("LLVM time-trace worker still has active scopes");
    }
    // Admission closes atomically with the validated snapshot for both API
    // flavors. A late legacy initialize starts a new epoch instead of joining
    // a generation whose trace has already been rendered.
    State->Sealed = true;

    {
      raw_svector_ostream BufferOS(Bytes);
      json::OStream J(BufferOS);
      J.objectBegin();
      J.attributeBegin("traceEvents");
      J.arrayBegin();

      // Emit all events for the main flame graph.
      auto writeEvent = [&](const auto &E, uint64_t Tid) {
        auto StartUs = E.getFlameGraphStartUs(StartTime);
        auto DurUs = E.getFlameGraphDurUs();

        J.object([&] {
          J.attribute("pid", Pid);
          J.attribute("tid", int64_t(Tid));
          J.attribute("ph", "X");
          J.attribute("ts", StartUs);
          J.attribute("dur", DurUs);
          J.attribute("name", E.Name);
          if (!E.Detail.empty()) {
            J.attributeObject("args", [&] { J.attribute("detail", E.Detail); });
          }
        });
      };
      for (const TimeTraceProfilerEntry &E : Entries)
        writeEvent(E, this->Tid);
      for (const auto &Entry : Instances.List) {
        if (Entry.Session != RequestedSession || Entry.Epoch != RequestedEpoch)
          continue;
        const TimeTraceProfiler *TTP = Entry.Profiler;
        for (const TimeTraceProfilerEntry &E : TTP->Entries)
          writeEvent(E, TTP->Tid);
      }

      // Emit totals by section name as additional "thread" events, sorted from
      // longest one.
      // Find highest used thread id.
      uint64_t MaxTid = this->Tid;
      for (const auto &Entry : Instances.List)
        if (Entry.Session == RequestedSession && Entry.Epoch == RequestedEpoch)
          MaxTid = std::max(MaxTid, Entry.Profiler->Tid);

      // Combine all CountAndTotalPerName from threads into one.
      StringMap<CountAndDurationType> AllCountAndTotalPerName;
      auto combineStat = [&](const auto &Stat) {
        StringRef Key = Stat.getKey();
        auto Value = Stat.getValue();
        auto &CountAndTotal = AllCountAndTotalPerName[Key];
        CountAndTotal.first += Value.first;
        CountAndTotal.second += Value.second;
      };
      for (const auto &Stat : CountAndTotalPerName)
        combineStat(Stat);
      for (const auto &Entry : Instances.List) {
        if (Entry.Session != RequestedSession || Entry.Epoch != RequestedEpoch)
          continue;
        for (const auto &Stat : Entry.Profiler->CountAndTotalPerName)
          combineStat(Stat);
      }

      SmallVector<NameAndCountAndDurationType, 16> SortedTotals;
      SortedTotals.reserve(AllCountAndTotalPerName.size());
      for (const auto &Total : AllCountAndTotalPerName)
        SortedTotals.emplace_back(Total.getKey(), Total.getValue());

      llvm::sort(SortedTotals, [](const NameAndCountAndDurationType &A,
                                  const NameAndCountAndDurationType &B) {
        return A.second.second > B.second.second;
      });

      // Report totals on separate threads of tracing file.
      uint64_t TotalTid = MaxTid + 1;
      for (const NameAndCountAndDurationType &Total : SortedTotals) {
        auto DurUs = duration_cast<microseconds>(Total.second.second).count();
        auto Count = AllCountAndTotalPerName[Total.first].first;

        J.object([&] {
          J.attribute("pid", Pid);
          J.attribute("tid", int64_t(TotalTid));
          J.attribute("ph", "X");
          J.attribute("ts", 0);
          J.attribute("dur", DurUs);
          J.attribute("name", (Twine("Total ") + Total.first).str());
          J.attributeObject("args", [&] {
            J.attribute("count", int64_t(Count));
            J.attribute("avg ms", int64_t(DurUs / Count / 1000));
          });
        });

        ++TotalTid;
      }

      auto writeMetadataEvent = [&](const char *Name, uint64_t Tid,
                                    StringRef arg) {
        J.object([&] {
          J.attribute("cat", "");
          J.attribute("pid", Pid);
          J.attribute("tid", int64_t(Tid));
          J.attribute("ts", 0);
          J.attribute("ph", "M");
          J.attribute("name", Name);
          J.attributeObject("args", [&] { J.attribute("name", arg); });
        });
      };

      writeMetadataEvent("process_name", Tid, ProcName);
      writeMetadataEvent("thread_name", Tid, ThreadName);
      for (const auto &Entry : Instances.List)
        if (Entry.Session == RequestedSession && Entry.Epoch == RequestedEpoch)
          writeMetadataEvent("thread_name", Entry.Profiler->Tid,
                             Entry.Profiler->ThreadName);

      J.arrayEnd();
      J.attributeEnd();

      // Emit the absolute time when this TimeProfiler started.
      // This can be used to combine the profiling data from
      // multiple processes and preserve actual time intervals.
      J.attribute("beginningOfTime",
                  time_point_cast<microseconds>(BeginningOfTime)
                      .time_since_epoch()
                      .count());

      J.objectEnd();
    }
    return Bytes;
  }

  // Write events from this TimeTraceProfilerInstance and its finished worker
  // profilers. Registry validation and JSON construction happen in render();
  // the caller-provided stream is touched only after the lock is released.
  Error write(raw_pwrite_stream &OS, TimeTraceProfilerSession RequestedSession,
              uint64_t RequestedEpoch) {
    auto Bytes = render(RequestedSession, RequestedEpoch);
    if (!Bytes)
      return Bytes.takeError();
    OS.write(Bytes->data(), Bytes->size());
    return Error::success();
  }

  SmallVector<TimeTraceProfilerEntry, 16> Stack;
  SmallVector<TimeTraceProfilerEntry, 128> Entries;
  StringMap<CountAndDurationType> CountAndTotalPerName;
  // System clock time when the session was begun.
  const time_point<system_clock> BeginningOfTime;
  // Profiling clock time when the session was begun.
  const TimePointType StartTime;
  const SmallString<64> ProcName;
  const sys::Process::Pid Pid;
  SmallString<0> ThreadName;
  const uint64_t Tid;

  // Minimum time granularity (in microseconds)
  const unsigned TimeTraceGranularity;
  const TimeTraceProfilerSession Session;
  uint64_t Epoch;
};

inline void timeTraceProfilerInitialize(unsigned TimeTraceGranularity,
                                        StringRef ProcName) {
  assert(TimeTraceProfilerInstance == 0 &&
         "Profiler should not be initialized");
  auto *Profiler = new TimeTraceProfiler(
      TimeTraceGranularity, llvm::sys::path::filename(ProcName), 0, 0);
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto *State = time_trace_detail::findOpenLegacyState(Instances);
  if (!State) {
    if (Instances.NextLegacyEpoch == ~uint64_t{0}) {
      LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
      delete Profiler;
      report_fatal_error("LLVM legacy time-trace epochs are exhausted");
    }
    time_trace_detail::TimeTraceProfilerSessionState NewState;
    NewState.Epoch = Instances.NextLegacyEpoch++;
    Instances.Sessions.push_back(std::move(NewState));
    State = &Instances.Sessions.back();
  }
  Profiler->Epoch = State->Epoch;
  if (!State->Root)
    State->Root = Profiler;
  ++State->LiveInstances;
  TimeTraceProfilerInstance = Profiler;
  TimeTraceProfilerInstanceSession = 0;
  LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
}

inline Error timeTraceProfilerInitializeSession(
    unsigned TimeTraceGranularity, StringRef ProcName,
    TimeTraceProfilerSession Session) {
  if (Session == 0)
    return createStringError(inconvertibleErrorCode(),
                             "managed LLVM time-trace session is zero");
  if (TimeTraceProfilerInstance)
    return createStringError(inconvertibleErrorCode(),
                             "LLVM time-trace profiler is already active");

  auto *Profiler = new TimeTraceProfiler(
      TimeTraceGranularity, llvm::sys::path::filename(ProcName), Session);
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto Unlock = llvm::make_scope_exit(
      [&] { LLVM_TP_MUTEX_UNLOCK(&Instances.Lock); });

  if (time_trace_detail::findSessionState(Instances, Session, 0)) {
    delete Profiler;
    return createStringError(inconvertibleErrorCode(),
                             "LLVM time-trace session is already registered");
  }
  for (const auto &Legacy : Instances.Sessions) {
    // A closing legacy generation can only discard its late workers. Its
    // epoch cannot contribute to this managed session, so it need not block a
    // fresh root after the old root has already relinquished ownership.
    if (Legacy.Session != 0 || Legacy.Closing)
      continue;
    const size_t Finished =
        time_trace_detail::countFinished(Instances, 0, Legacy.Epoch);
    if (Legacy.LiveInstances != Finished) {
      delete Profiler;
      return createStringError(
          inconvertibleErrorCode(),
          "a foreign legacy LLVM time-trace profiler is active");
    }
  }
  for (const auto &State : Instances.Sessions) {
    if (State.Session != 0 && State.Root && !State.Closing) {
      delete Profiler;
      return createStringError(inconvertibleErrorCode(),
                               "another LLVM time-trace root is active");
    }
  }

  time_trace_detail::TimeTraceProfilerSessionState State;
  State.Session = Session;
  State.Epoch = 0;
  State.LiveInstances = 1;
  State.Root = Profiler;
  Instances.Sessions.push_back(State);
  TimeTraceProfilerInstance = Profiler;
  TimeTraceProfilerInstanceSession = Session;
  return Error::success();
}

inline Error timeTraceProfilerInitializeThread(
    unsigned TimeTraceGranularity, StringRef ProcName,
    TimeTraceProfilerSession Session) {
  if (Session == 0)
    return createStringError(inconvertibleErrorCode(),
                             "managed LLVM worker session is zero");
  if (TimeTraceProfilerInstance)
    return createStringError(inconvertibleErrorCode(),
                             "LLVM time-trace profiler is already active");

  auto *Profiler = new TimeTraceProfiler(
      TimeTraceGranularity, llvm::sys::path::filename(ProcName), Session);
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto Unlock = llvm::make_scope_exit(
      [&] { LLVM_TP_MUTEX_UNLOCK(&Instances.Lock); });
  auto *State = time_trace_detail::findSessionState(Instances, Session, 0);
  if (!State || !State->Root || State->Closing || State->Sealed) {
    delete Profiler;
    return createStringError(inconvertibleErrorCode(),
                             "LLVM time-trace session is not open");
  }
  ++State->LiveInstances;
  TimeTraceProfilerInstance = Profiler;
  TimeTraceProfilerInstanceSession = Session;
  return Error::success();
}

inline void timeTraceProfilerCleanupCurrentThread() {
  TimeTraceProfiler *Profiler = TimeTraceProfilerInstance;
  if (!Profiler)
    return;
  const TimeTraceProfilerSession Session = TimeTraceProfilerInstanceSession;

  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto *State = time_trace_detail::findSessionState(Instances, Session,
                                                     Profiler->Epoch);
  if (State) {
    if (State->Root == Profiler)
      State->Root = nullptr;
    assert(State->LiveInstances != 0 &&
           "LLVM time-trace live count underflow");
    --State->LiveInstances;
    if (State->LiveInstances == 0) {
      auto It = llvm::find_if(Instances.Sessions, [State](const auto &Entry) {
        return &Entry == State;
      });
      Instances.Sessions.erase(It);
    }
  }
  TimeTraceProfilerInstance = nullptr;
  TimeTraceProfilerInstanceSession = 0;
  LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
  delete Profiler;
}

inline void
timeTraceProfilerCleanupSession(TimeTraceProfilerSession Session) {
  if (Session == 0)
    return;
  SmallVector<TimeTraceProfiler *, 8> DeleteAfterUnlock;
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto *State = time_trace_detail::findSessionState(Instances, Session, 0);
  if (!State) {
    LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
    return;
  }
  State->Closing = true;

  if (TimeTraceProfilerInstance &&
      TimeTraceProfilerInstanceSession == Session) {
    if (State->Root == TimeTraceProfilerInstance)
      State->Root = nullptr;
    DeleteAfterUnlock.push_back(TimeTraceProfilerInstance);
    TimeTraceProfilerInstance = nullptr;
    TimeTraceProfilerInstanceSession = 0;
    assert(State->LiveInstances != 0 &&
           "LLVM time-trace live count underflow");
    --State->LiveInstances;
  }

  for (auto It = Instances.List.begin(); It != Instances.List.end();) {
    if (It->Session != Session || It->Epoch != 0) {
      ++It;
      continue;
    }
    DeleteAfterUnlock.push_back(It->Profiler);
    assert(State->LiveInstances != 0 &&
           "LLVM time-trace live count underflow");
    --State->LiveInstances;
    It = Instances.List.erase(It);
  }
  if (State->LiveInstances == 0) {
    auto It = llvm::find_if(Instances.Sessions, [State](const auto &Entry) {
      return &Entry == State;
    });
    Instances.Sessions.erase(It);
  }
  LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
  for (TimeTraceProfiler *Profiler : DeleteAfterUnlock)
    delete Profiler;
}

// Removes only legacy/raw TimeTraceProfilerInstances. Called from the legacy
// root thread; explicitly managed sessions are isolated from this API.
inline void timeTraceProfilerCleanup() {
  SmallVector<TimeTraceProfiler *, 8> DeleteAfterUnlock;
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  TimeTraceProfiler *Current =
      TimeTraceProfilerInstanceSession == 0 ? TimeTraceProfilerInstance
                                            : nullptr;
  const uint64_t CurrentEpoch = Current ? Current->Epoch : 0;
  auto *State = Current
                    ? time_trace_detail::findSessionState(Instances, 0,
                                                          CurrentEpoch)
                    : nullptr;
  if (State)
    State->Closing = true;
  if (Current) {
    if (State && State->Root == TimeTraceProfilerInstance)
      State->Root = nullptr;
    DeleteAfterUnlock.push_back(TimeTraceProfilerInstance);
    TimeTraceProfilerInstance = nullptr;
    TimeTraceProfilerInstanceSession = 0;
    if (State && State->LiveInstances)
      --State->LiveInstances;
  }
  for (auto It = Instances.List.begin(); It != Instances.List.end();) {
    if (It->Session != 0 || (Current && It->Epoch != CurrentEpoch)) {
      ++It;
      continue;
    }
    if (!Current) {
      auto *EntryState =
          time_trace_detail::findSessionState(Instances, 0, It->Epoch);
      if (!EntryState || EntryState->Root ||
          EntryState->LiveInstances !=
              time_trace_detail::countFinished(Instances, 0, It->Epoch)) {
        ++It;
        continue;
      }
      EntryState->Closing = true;
    }
    DeleteAfterUnlock.push_back(It->Profiler);
    auto *EntryState =
        time_trace_detail::findSessionState(Instances, 0, It->Epoch);
    if (EntryState && EntryState->LiveInstances)
      --EntryState->LiveInstances;
    It = Instances.List.erase(It);
  }
  for (auto It = Instances.Sessions.begin(); It != Instances.Sessions.end();) {
    if (It->Session == 0 && It->LiveInstances == 0 &&
        (It->Closing || (!It->Root && !Current))) {
      It = Instances.Sessions.erase(It);
      continue;
    }
    ++It;
  }
  LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
  for (TimeTraceProfiler *Profiler : DeleteAfterUnlock)
    delete Profiler;
}

// Finish TimeTraceProfilerInstance on a worker thread.
// This doesn't remove the instance, just moves the pointer to global vector.
inline void timeTraceProfilerFinishThread() {
  TimeTraceProfiler *Profiler = TimeTraceProfilerInstance;
  assert(Profiler && "Profiler should be initialized before finishing");
  const TimeTraceProfilerSession Session = TimeTraceProfilerInstanceSession;
  const uint64_t Epoch = Profiler->Epoch;
  auto &Instances = time_trace_detail::getTimeTraceProfilerInstances();
  LLVM_TP_MUTEX_LOCK(&Instances.Lock);
  auto *State =
      time_trace_detail::findSessionState(Instances, Session, Epoch);
  const bool Publish =
      State && !State->Closing && (Session == 0 || State->Root != Profiler);
  if (Publish) {
    if (State->Root == Profiler)
      State->Root = nullptr;
    Instances.List.push_back({Profiler, Session, Epoch});
  } else if (State) {
    if (State->Root == Profiler)
      State->Root = nullptr;
    assert(State->LiveInstances != 0 &&
           "LLVM time-trace live count underflow");
    --State->LiveInstances;
    if (State->LiveInstances == 0) {
      auto It = llvm::find_if(Instances.Sessions, [State](const auto &Entry) {
        return &Entry == State;
      });
      Instances.Sessions.erase(It);
    }
  }
  TimeTraceProfilerInstance = 0;
  TimeTraceProfilerInstanceSession = 0;
  LLVM_TP_MUTEX_UNLOCK(&Instances.Lock);
  if (!Publish)
    delete Profiler;
}

inline void timeTraceProfilerWrite(raw_pwrite_stream &OS) {
  assert(TimeTraceProfilerInstance != 0 && "Profiler object can't be null");
  if (Error WriteError = TimeTraceProfilerInstance->write(
          OS, 0, TimeTraceProfilerInstance->Epoch))
    report_fatal_error(Twine("cannot write LLVM time trace: ") +
                           toString(std::move(WriteError)),
                       /*gen_crash_diag=*/false);
}

inline Error timeTraceProfilerWriteSession(raw_pwrite_stream &OS,
                                           TimeTraceProfilerSession Session) {
  if (!TimeTraceProfilerInstance || Session == 0)
    return createStringError(inconvertibleErrorCode(),
                             "managed LLVM time-trace profiler is not active");
  return TimeTraceProfilerInstance->write(OS, Session, 0);
}

inline Error timeTraceProfilerWrite(StringRef PreferredFileName,
                                    StringRef FallbackFileName) {
  assert(TimeTraceProfilerInstance != 0 && "Profiler object can't be null");

  SmallString<256> Path(PreferredFileName.str());
  if (Path.empty()) {
    Path = FallbackFileName == "-" ? "out" : FallbackFileName.str();
    Path += ".time-trace";
  }

  // Render before opening the destination. A validation failure must not
  // create or truncate the requested trace file.
  auto Bytes = TimeTraceProfilerInstance->render(
      /*RequestedSession=*/0, TimeTraceProfilerInstance->Epoch);
  if (!Bytes)
    return Bytes.takeError();

  std::error_code EC;
  raw_fd_ostream OS(Path, EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    return createStringError(EC, "Could not open " + Path);

  OS.write(Bytes->data(), Bytes->size());
  if (Path == "-")
    OS.flush();
  else
    OS.close();
  if (OS.has_error()) {
    std::error_code WriteError = OS.error();
    OS.clear_error();
    return createStringError(
        WriteError, llvm::Twine("Could not write LLVM time trace ") + Path +
                        ": " + WriteError.message());
  }
  return Error::success();
}

inline void timeTraceProfilerBegin(StringRef Name, StringRef Detail) {
  if (TimeTraceProfilerInstance != 0)
    TimeTraceProfilerInstance->begin(SmallString<64>(Name),
                                     [&]() { return SmallString<64>(Detail); });
}

inline void
timeTraceProfilerBegin(StringRef Name,
                       llvm::function_ref<SmallString<64>()> Detail) {
  if (TimeTraceProfilerInstance != 0)
    TimeTraceProfilerInstance->begin(SmallString<64>(Name), Detail);
}

inline void timeTraceProfilerEnd() {
  if (TimeTraceProfilerInstance != 0)
    TimeTraceProfilerInstance->end();
}

} // namespace llvm

#endif
