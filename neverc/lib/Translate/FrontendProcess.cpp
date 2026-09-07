#include "FrontendProcess.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

using namespace llvm;
namespace neverc::translate {
namespace {
static std::atomic<bool> Interrupted{false};
static_assert(std::atomic<bool>::is_always_lock_free,
              "signal cancellation requires lock-free atomic storage");
void interrupt() { Interrupted.store(true, std::memory_order_relaxed); }

std::string terminateChild(const sys::ProcessInfo &Child) {
  std::string Error;
#ifdef _WIN32
  if (!TerminateProcess(Child.Process, 1))
    Error = std::error_code(GetLastError(), std::system_category()).message();
#else
  // This is the exact child returned by ExecuteNoWait, never a process group.
  if (::kill(Child.Pid, SIGKILL) != 0 && errno != ESRCH)
    Error = std::error_code(errno, std::generic_category()).message();
#endif

  // LLVM's non-polling timeout wait kills and then waits without a bound.
  // A macOS process stuck exiting in the kernel may never become reapable,
  // even after SIGKILL. Give ordinary children a short chance to be reaped,
  // but never let an OS-unreapable child block translation cleanup.
  const auto Deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
  for (;;) {
    auto Status = sys::Wait(Child, 0, nullptr, nullptr, true);
    if (Status.Pid != 0)
      return {}; // Wait also closes a completed Windows process handle.
    const auto Now = std::chrono::steady_clock::now();
    if (Now >= Deadline)
      break;
    std::this_thread::sleep_until(
        std::min(Deadline, Now + std::chrono::milliseconds(20)));
  }
#ifdef _WIN32
  // The process can outlive the cleanup deadline; our handle must not.
  CloseHandle(Child.Process);
#endif
  return Error.empty()
             ? "child did not exit within the bounded cleanup interval"
             : "could not terminate child: " + Error;
}
} // namespace
TranslationCancellation::TranslationCancellation() {
  Interrupted.store(false, std::memory_order_relaxed);
  sys::SetInterruptFunction(interrupt);
}
TranslationCancellation::~TranslationCancellation() {
  sys::SetInterruptFunction(nullptr);
}
bool translationCancelled() {
  return Interrupted.load(std::memory_order_relaxed);
}
ProcessResult runProcess(const std::vector<std::string> &Arguments,
                         StringRef Stdout, StringRef Stderr,
                         unsigned TimeoutSeconds) {
  if (translationCancelled())
    return {-2, true, "translation cancelled"};
  SmallVector<StringRef, 16> Args;
  for (const auto &A : Arguments)
    Args.push_back(A);
  SmallVector<char, 256> Message;
  bool Failed = false;
  StringRef Redirects[] = {"", Stdout, Stderr};
  // Source context and runtime provenance must not depend on inherited include
  // paths, deployment targets, loader overrides or compiler configuration.
  // Preserve only the OS environment needed to launch installed tools.
  std::vector<std::string> Environment{"LC_ALL=C",
                                       "NEVERC_NO_DEFAULT_CONFIG=1"};
  for (const char *Name :
       {"PATH", "HOME", "USERPROFILE", "SystemRoot", "SystemDrive", "COMSPEC",
        "PATHEXT", "TMPDIR", "TMP", "TEMP"})
    if (auto Value = sys::Process::GetEnv(Name))
      Environment.push_back(std::string(Name) + "=" + Value->str().str());
  SmallVector<StringRef, 16> EnvironmentRefs;
  for (const auto &Entry : Environment)
    EnvironmentRefs.push_back(Entry);
  auto Child = sys::ExecuteNoWait(Args.front(), Args, EnvironmentRefs,
                                  Redirects, 0, &Message, &Failed);
  if (Failed)
    return {-1, false, std::string(Message.begin(), Message.end())};
  const auto Start = std::chrono::steady_clock::now();
  for (;;) {
    auto Status = sys::Wait(Child, 0, &Message, nullptr, true);
    if (Status.Pid != 0)
      return {Status.ReturnCode, false,
              std::string(Message.begin(), Message.end())};
    const bool Cancelled = Interrupted.load(std::memory_order_relaxed);
    if (Cancelled || std::chrono::steady_clock::now() - Start >=
                         std::chrono::seconds(TimeoutSeconds)) {
      std::string Error =
          Cancelled ? "translation cancelled" : "process timed out";
      std::string CleanupError = terminateChild(Child);
      if (!CleanupError.empty())
        Error += "; " + CleanupError;
      return {-2, Cancelled, std::move(Error)};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}
} // namespace neverc::translate
