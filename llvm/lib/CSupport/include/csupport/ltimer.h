#ifndef CSUPPORT_LTIMER_H
#define CSUPPORT_LTIMER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Format a timer value for printing: "  1.2345 (42.1%)" or "        ----- ".
   Returns chars written. */
int csupport_format_timer_print_val(char *buf, size_t buflen, double val,
                                    double total);

uint64_t csupport_get_instructions_executed(void);

#ifdef __cplusplus
}

/* ===== Timer C++ inline implementations (from cpp_bridge.cpp) ===== */

#include "cpp_compat_stl.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signposts.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_PROC_PID_RUSAGE
#include <libproc.h>
#endif

namespace llvm {
namespace timer_detail {

inline ManagedStatic<SmallString<256>> LibSupportInfoOutputFilename;
inline SmallString<256> &getLibSupportInfoOutputFilename() {
  return *LibSupportInfoOutputFilename;
}

inline ManagedStatic<sys::SmartMutex<true>> TimerLock;
inline ManagedStatic<SignpostEmitter> Signposts;

struct CreateTrackSpace {
  static void *call() {
    return new cl::opt<bool>("track-memory",
                             cl::desc("Enable -time-passes memory "
                                      "tracking (this may be slow)"),
                             cl::Hidden);
  }
};
inline ManagedStatic<cl::opt<bool>, CreateTrackSpace> TrackSpace;

struct CreateInfoOutputFilename {
  static void *call() {
    return new cl::opt<SmallString<256>, true>(
        "info-output-file", cl::value_desc("filename"),
        cl::desc("File to append -stats and -timer output to"), cl::Hidden,
        cl::location(getLibSupportInfoOutputFilename()));
  }
};
inline ManagedStatic<cl::opt<SmallString<256>, true>, CreateInfoOutputFilename>
    InfoOutputFilename;

struct CreateSortTimers {
  static void *call() {
    return new cl::opt<bool>(
        "sort-timers",
        cl::desc("In the report, sort the timers in each group "
                 "in wall clock time order"),
        cl::init(true), cl::Hidden);
  }
};
inline ManagedStatic<cl::opt<bool>, CreateSortTimers> SortTimers;

struct CreateDefaultTimerGroup {
  static void *call() {
    return new TimerGroup("misc", "Miscellaneous Ungrouped Timers");
  }
};
inline ManagedStatic<TimerGroup, CreateDefaultTimerGroup> DefaultTimerGroup;
inline TimerGroup *getDefaultTimerGroup() { return &*DefaultTimerGroup; }

inline size_t getTimerMemUsage() {
  if (!*TrackSpace)
    return 0;
  return sys::Process::GetMallocUsage();
}

inline void timerPrintVal(double Val, double Total, raw_ostream &OS) {
  char buf[64];
  csupport_format_timer_print_val(buf, sizeof(buf), Val, Total);
  OS << buf;
}

typedef StringMap<Timer> Name2TimerMap;
struct TimerGroupEntry {
  TimerGroup *first;
  Name2TimerMap second;
};

class Name2PairMap {
  StringMap<TimerGroupEntry> Map;

public:
  ~Name2PairMap() {
    for (auto I = Map.begin(), E = Map.end(); I != E; ++I)
      delete I->second.first;
  }
  Timer &get(StringRef Name, StringRef Description, StringRef GroupName,
             StringRef GroupDescription) {
    sys::SmartScopedLock<true> L(*TimerLock);
    auto &GroupEntry = Map[GroupName];
    if (!GroupEntry.first)
      GroupEntry.first = new TimerGroup(GroupName, GroupDescription);
    Timer &T = GroupEntry.second[Name];
    if (!T.isInitialized())
      T.init(Name, Description, *GroupEntry.first);
    return T;
  }
};

inline ManagedStatic<Name2PairMap> NamedGroupedTimers;
inline TimerGroup *TimerGroupList = 0;

} // namespace timer_detail

inline void initTimerOptions() {
  *timer_detail::TrackSpace;
  *timer_detail::InfoOutputFilename;
  *timer_detail::SortTimers;
}

inline uptr_t<raw_fd_ostream> CreateInfoOutputFile() {
  const SmallString<256> &OutputFilename =
      timer_detail::getLibSupportInfoOutputFilename();
  if (OutputFilename.empty())
    return uptr_t<raw_fd_ostream>(new raw_fd_ostream(2, false));
  if (OutputFilename == "-")
    return uptr_t<raw_fd_ostream>(new raw_fd_ostream(1, false));
  errc_t EC;
  auto Result = uptr_t<raw_fd_ostream>(new raw_fd_ostream(
      OutputFilename, EC, sys::fs::OF_Append | sys::fs::OF_TextWithCRLF));
  if (!EC)
    return Result;
  errs() << "Error opening info-output-file '" << OutputFilename
         << " for appending!\n";
  return uptr_t<raw_fd_ostream>(new raw_fd_ostream(2, false));
}

inline void Timer::init(StringRef TimerName, StringRef TimerDescription) {
  init(TimerName, TimerDescription, *timer_detail::getDefaultTimerGroup());
}

inline void Timer::init(StringRef TimerName, StringRef TimerDescription,
                        TimerGroup &tg) {
  assert(!TG && "Timer already initialized");
  Name.assign(TimerName.begin(), TimerName.end());
  Description.assign(TimerDescription.begin(), TimerDescription.end());
  Running = Triggered = false;
  TG = &tg;
  TG->addTimer(*this);
}

inline Timer::~Timer() {
  if (!TG)
    return;
  TG->removeTimer(*this);
}

inline TimeRecord TimeRecord::getCurrentTime(bool Start) {
  using Seconds = std::chrono::duration<double, ratio_t<1>>;
  using Nanos = chrono_ns;
  TimeRecord Result;
  sys::TimePoint<> now;
  Nanos user_ns, sys_ns;
  if (Start) {
    Result.MemUsed = timer_detail::getTimerMemUsage();
    Result.InstructionsExecuted = csupport_get_instructions_executed();
    sys::Process::GetTimeUsage(now, user_ns, sys_ns);
  } else {
    sys::Process::GetTimeUsage(now, user_ns, sys_ns);
    Result.InstructionsExecuted = csupport_get_instructions_executed();
    Result.MemUsed = timer_detail::getTimerMemUsage();
  }
  Result.WallTime = Seconds(now.time_since_epoch()).count();
  Result.UserTime = Seconds(user_ns).count();
  Result.SystemTime = Seconds(sys_ns).count();
  return Result;
}

inline void Timer::startTimer() {
  assert(!Running && "Cannot start a running timer");
  Running = Triggered = true;
  timer_detail::Signposts->startInterval(this, getName());
  StartTime = TimeRecord::getCurrentTime(true);
}

inline void Timer::stopTimer() {
  assert(Running && "Cannot stop a paused timer");
  Running = false;
  Time += TimeRecord::getCurrentTime(false);
  Time -= StartTime;
  timer_detail::Signposts->endInterval(this, getName());
}

inline void Timer::clear() {
  Running = Triggered = false;
  Time = StartTime = TimeRecord();
}

inline void TimeRecord::print(const TimeRecord &Total, raw_ostream &OS) const {
  if (Total.getUserTime())
    timer_detail::timerPrintVal(getUserTime(), Total.getUserTime(), OS);
  if (Total.getSystemTime())
    timer_detail::timerPrintVal(getSystemTime(), Total.getSystemTime(), OS);
  if (Total.getProcessTime())
    timer_detail::timerPrintVal(getProcessTime(), Total.getProcessTime(), OS);
  timer_detail::timerPrintVal(getWallTime(), Total.getWallTime(), OS);
  OS << "  ";
  if (Total.getMemUsed())
    OS << format("%9" PRId64 "  ", (int64_t)getMemUsed());
  if (Total.getInstructionsExecuted())
    OS << format("%9" PRId64 "  ", (int64_t)getInstructionsExecuted());
}

inline NamedRegionTimer::NamedRegionTimer(StringRef Name, StringRef Description,
                                          StringRef GroupName,
                                          StringRef GroupDescription,
                                          bool Enabled)
    : TimeRegion(!Enabled
                     ? 0
                     : &timer_detail::NamedGroupedTimers->get(
                           Name, Description, GroupName, GroupDescription)) {}

inline TimerGroup::TimerGroup(StringRef Name, StringRef Description)
    : Name(Name.begin(), Name.end()),
      Description(Description.begin(), Description.end()) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  if (timer_detail::TimerGroupList)
    timer_detail::TimerGroupList->Prev = &Next;
  Next = timer_detail::TimerGroupList;
  Prev = &timer_detail::TimerGroupList;
  timer_detail::TimerGroupList = this;
}

inline TimerGroup::TimerGroup(StringRef Name, StringRef Description,
                              const StringMap<TimeRecord> &Records)
    : TimerGroup(Name, Description) {
  TimersToPrint.reserve(Records.size());
  for (const auto &P : Records)
    TimersToPrint.emplace_back(P.getValue(), P.getKey(), P.getKey());
  assert(TimersToPrint.size() == Records.size() && "Size mismatch");
}

inline TimerGroup::~TimerGroup() {
  while (FirstTimer)
    removeTimer(*FirstTimer);
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  *Prev = Next;
  if (Next)
    Next->Prev = Prev;
}

inline void TimerGroup::removeTimer(Timer &T) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  if (T.hasTriggered())
    TimersToPrint.emplace_back(T.Time, T.Name, T.Description);
  T.TG = 0;
  *T.Prev = T.Next;
  if (T.Next)
    T.Next->Prev = T.Prev;
  if (FirstTimer || TimersToPrint.empty())
    return;
  uptr_t<raw_ostream> OutStream = CreateInfoOutputFile();
  PrintQueuedTimers(*OutStream);
}

inline void TimerGroup::addTimer(Timer &T) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  if (FirstTimer)
    FirstTimer->Prev = &T.Next;
  T.Next = FirstTimer;
  T.Prev = &FirstTimer;
  FirstTimer = &T;
}

inline void TimerGroup::PrintQueuedTimers(raw_ostream &OS) {
  if (*timer_detail::SortTimers)
    llvm::sort(TimersToPrint);
  TimeRecord Total;
  for (const PrintRecord &Record : TimersToPrint)
    Total += Record.Time;
  char tdashes[74];
  memset(tdashes, '-', 73);
  tdashes[73] = '\0';
  OS << "===" << tdashes << "===\n";
  unsigned Padding = (80 - Description.size()) / 2;
  if (Padding > 80)
    Padding = 0;
  OS.indent(Padding) << Description << '\n';
  OS << "===" << tdashes << "===\n";
  if (this != timer_detail::getDefaultTimerGroup())
    OS << format("  Total Execution Time: %5.4f seconds (%5.4f wall clock)\n",
                 Total.getProcessTime(), Total.getWallTime());
  OS << '\n';
  if (Total.getUserTime())
    OS << "   ---User Time---";
  if (Total.getSystemTime())
    OS << "   --System Time--";
  if (Total.getProcessTime())
    OS << "   --User+System--";
  OS << "   ---Wall Time---";
  if (Total.getMemUsed())
    OS << "  ---Mem---";
  if (Total.getInstructionsExecuted())
    OS << "  ---Instr---";
  OS << "  --- Name ---\n";
  for (const PrintRecord &Record : llvm::reverse(TimersToPrint)) {
    Record.Time.print(Total, OS);
    OS << Record.Description << '\n';
  }
  Total.print(Total, OS);
  OS << "Total\n\n";
  OS.flush();
  TimersToPrint.clear();
}

inline void TimerGroup::prepareToPrintList(bool ResetTime) {
  for (Timer *T = FirstTimer; T; T = T->Next) {
    if (!T->hasTriggered())
      continue;
    bool WasRunning = T->isRunning();
    if (WasRunning)
      T->stopTimer();
    TimersToPrint.emplace_back(T->Time, T->Name, T->Description);
    if (ResetTime)
      T->clear();
    if (WasRunning)
      T->startTimer();
  }
}

inline void TimerGroup::print(raw_ostream &OS, bool ResetAfterPrint) {
  {
    sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
    prepareToPrintList(ResetAfterPrint);
  }
  if (!TimersToPrint.empty())
    PrintQueuedTimers(OS);
}

inline void TimerGroup::clear() {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  for (Timer *T = FirstTimer; T; T = T->Next)
    T->clear();
}

inline void TimerGroup::printAll(raw_ostream &OS) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  for (TimerGroup *TG = timer_detail::TimerGroupList; TG; TG = TG->Next)
    TG->print(OS);
}

inline void TimerGroup::clearAll() {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  for (TimerGroup *TG = timer_detail::TimerGroupList; TG; TG = TG->Next)
    TG->clear();
}

inline void TimerGroup::printJSONValue(raw_ostream &OS, const PrintRecord &R,
                                       const char *suffix, double Value) {
  assert(yaml::needsQuotes(Name) == yaml::QuotingType::None &&
         "TimerGroup name should not need quotes");
  assert(yaml::needsQuotes(R.Name) == yaml::QuotingType::None &&
         "Timer name should not need quotes");
  const int max_digits10 = 17;
  OS << "\t\"time." << Name << '.' << R.Name << suffix
     << "\": " << format("%.*e", max_digits10 - 1, Value);
}

inline const char *TimerGroup::printJSONValues(raw_ostream &OS,
                                               const char *delim) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  prepareToPrintList(false);
  for (const PrintRecord &R : TimersToPrint) {
    OS << delim;
    delim = ",\n";
    const TimeRecord &T = R.Time;
    printJSONValue(OS, R, ".wall", T.getWallTime());
    OS << delim;
    printJSONValue(OS, R, ".user", T.getUserTime());
    OS << delim;
    printJSONValue(OS, R, ".sys", T.getSystemTime());
    if (T.getMemUsed()) {
      OS << delim;
      printJSONValue(OS, R, ".mem", T.getMemUsed());
    }
    if (T.getInstructionsExecuted()) {
      OS << delim;
      printJSONValue(OS, R, ".instr", T.getInstructionsExecuted());
    }
  }
  TimersToPrint.clear();
  return delim;
}

inline const char *TimerGroup::printAllJSONValues(raw_ostream &OS,
                                                  const char *delim) {
  sys::SmartScopedLock<true> L(*timer_detail::TimerLock);
  for (TimerGroup *TG = timer_detail::TimerGroupList; TG; TG = TG->Next)
    delim = TG->printJSONValues(OS, delim);
  return delim;
}

inline void TimerGroup::constructForStatistics() {
  (void)timer_detail::getLibSupportInfoOutputFilename();
  (void)*timer_detail::NamedGroupedTimers;
}

inline uptr_t<TimerGroup> TimerGroup::aquireDefaultGroup() {
  return uptr_t<TimerGroup>(timer_detail::DefaultTimerGroup.claim());
}

} // namespace llvm

#endif /* __cplusplus */
#endif /* CSUPPORT_LTIMER_H */
