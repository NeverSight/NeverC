#ifndef LINKER_CORE_RUNTIME_CRASHRECOVERY_H
#define LINKER_CORE_RUNTIME_CRASHRECOVERY_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TimeProfiler.h"

#include <memory>
#include <optional>
#include <string>

namespace linker::crash_recovery_detail {

class TimeTraceProfilerCrashCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<
          TimeTraceProfilerCrashCleanup, llvm::TimeTraceProfiler> {
public:
  TimeTraceProfilerCrashCleanup(llvm::CrashRecoveryContext *Context,
                                llvm::TimeTraceProfiler *Profiler)
      : llvm::CrashRecoveryContextCleanupBase<TimeTraceProfilerCrashCleanup,
                                              llvm::TimeTraceProfiler>(
            Context, Profiler) {}

  void recoverResources() override {
    if (llvm::getTimeTraceProfilerInstance() == this->resource)
      llvm::timeTraceProfilerCleanup();
  }
};

/// Owns a newly initialized time-trace profiler while preserving any profiler
/// supplied by an outer caller. Construct this before backend ownership so
/// crash-recovery cleanup tears down backend workers before the profiler. An
/// outer profiler that permits nested fatal recovery must itself be owned by a
/// cleanup registered with the same crash-recovery context.
class CrashRecoveryTimeTraceOwner {
public:
  CrashRecoveryTimeTraceOwner(bool Enable, unsigned Granularity,
                              llvm::StringRef ProcessName) {
    if (!Enable || llvm::timeTraceProfilerEnabled())
      return;
    llvm::timeTraceProfilerInitialize(Granularity, ProcessName);
    Profiler = llvm::getTimeTraceProfilerInstance();
    CrashCleanup.emplace(Profiler);
  }

  ~CrashRecoveryTimeTraceOwner() {
    CrashCleanup.reset();
    if (Profiler && llvm::getTimeTraceProfilerInstance() == Profiler)
      llvm::timeTraceProfilerCleanup();
  }

  CrashRecoveryTimeTraceOwner(const CrashRecoveryTimeTraceOwner &) = delete;
  CrashRecoveryTimeTraceOwner &
  operator=(const CrashRecoveryTimeTraceOwner &) = delete;
  CrashRecoveryTimeTraceOwner(CrashRecoveryTimeTraceOwner &&) = delete;
  CrashRecoveryTimeTraceOwner &
  operator=(CrashRecoveryTimeTraceOwner &&) = delete;

  bool ownsProfiler() const { return Profiler != nullptr; }

  llvm::Error write(llvm::StringRef OutputFile) const {
    if (!ownsProfiler())
      return llvm::Error::success();
    if (llvm::getTimeTraceProfilerInstance() != Profiler)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "linker time-trace profiler ownership changed before write");
    return llvm::timeTraceProfilerWrite(std::string(), OutputFile);
  }

private:
  llvm::TimeTraceProfiler *Profiler = nullptr;
  std::optional<llvm::CrashRecoveryContextCleanupRegistrar<
      llvm::TimeTraceProfiler, TimeTraceProfilerCrashCleanup>>
      CrashCleanup;
};

/// Borrows an external resource when supplied. Otherwise, owns a local
/// resource on the stack in the normal path and allocates it on the heap
/// only while an LLVM crash-recovery context is active. Crash recovery skips
/// stack destructors, so the registered delete cleanup is the only safe owner
/// after a longjmp. The protected work and recovery context must run on the
/// same thread, as required by thread-affine linker resources.
template <typename T> class CrashRecoveryLocalOwner {
public:
  explicit CrashRecoveryLocalOwner(T *External) : Active(External) {
    if (Active)
      return;

    if (llvm::CrashRecoveryContext::GetCurrent()) {
      CrashOwned = std::make_unique<T>();
      CrashCleanup.emplace(CrashOwned.get());
      Active = CrashOwned.get();
      return;
    }

    Local.emplace();
    Active = &*Local;
  }

  CrashRecoveryLocalOwner(const CrashRecoveryLocalOwner &) = delete;
  CrashRecoveryLocalOwner &operator=(const CrashRecoveryLocalOwner &) = delete;
  CrashRecoveryLocalOwner(CrashRecoveryLocalOwner &&) = delete;
  CrashRecoveryLocalOwner &operator=(CrashRecoveryLocalOwner &&) = delete;

  T &get() const { return *Active; }

private:
  std::optional<T> Local;
  std::unique_ptr<T> CrashOwned;
  std::optional<llvm::CrashRecoveryContextCleanupRegistrar<T>> CrashCleanup;
  T *Active = nullptr;
};

/// Tears down a context's backend without taking ownership of the context.
/// Register this after the time-trace cleanup so crash recovery joins backend
/// workers before it clears the profiler's worker registry.
template <typename T>
class CrashRecoveryDestroyBackendCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<
          CrashRecoveryDestroyBackendCleanup<T>, T> {
public:
  CrashRecoveryDestroyBackendCleanup(llvm::CrashRecoveryContext *Context,
                                     T *Resource)
      : llvm::CrashRecoveryContextCleanupBase<
            CrashRecoveryDestroyBackendCleanup<T>, T>(Context, Resource) {}

  void recoverResources() override { this->resource->destroyBackend(); }
};

} // namespace linker::crash_recovery_detail

#endif // LINKER_CORE_RUNTIME_CRASHRECOVERY_H
