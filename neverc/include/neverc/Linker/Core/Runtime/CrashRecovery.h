#ifndef LINKER_CORE_RUNTIME_CRASHRECOVERY_H
#define LINKER_CORE_RUNTIME_CRASHRECOVERY_H

#include "neverc/Foundation/Core/LLVMTimeTraceRootLease.h"
#include "llvm/Support/CrashRecoveryContext.h"

#include <memory>
#include <optional>

namespace linker::crash_recovery_detail {

/// Owns one process-exclusive root time-trace profiler or borrows a
/// same-thread root already managed by an outer owner. Concurrent roots and
/// unmanaged ambient profilers are rejected because LLVM stores finished
/// worker profilers in one process-global registry. Construct this before
/// backend ownership so crash-recovery cleanup tears down backend workers
/// before the profiler. The protected work and recovery context must run on
/// the same thread because LLVM profiler TLS is thread-affine, so
/// RunSafelyOnThread is intentionally unsupported.
class CrashRecoveryTimeTraceOwner
    : public neverc::LLVMTimeTraceProfilerOwner {
public:
  CrashRecoveryTimeTraceOwner(unsigned Granularity,
                              llvm::StringRef ProcessName)
      : neverc::LLVMTimeTraceProfilerOwner(Granularity, ProcessName) {}

  /// Returns a stable user-facing diagnostic when the requested root trace
  /// could not be acquired. Linker diagnostics remain stable even though the
  /// underlying lease is shared with the frontend.
  llvm::StringRef acquisitionError() const {
    switch (state()) {
    case neverc::LLVMTimeTraceRootLeaseState::Busy:
      return "neverc: error: cannot start linker time trace: another traced "
             "in-process link is already active";
    case neverc::LLVMTimeTraceRootLeaseState::UnmanagedAmbient:
      return "neverc: error: cannot start linker time trace: the current "
             "thread has an unmanaged LLVM time-trace profiler";
    case neverc::LLVMTimeTraceRootLeaseState::Inconsistent:
      return "neverc: error: cannot start linker time trace: managed profiler "
             "state is inconsistent";
    case neverc::LLVMTimeTraceRootLeaseState::Owned:
      if (!neverc::LLVMTimeTraceProfilerOwner::acquisitionError().empty())
        return "neverc: error: cannot start linker time trace: profiler "
               "initialization failed";
      return {};
    case neverc::LLVMTimeTraceRootLeaseState::Borrowed:
    case neverc::LLVMTimeTraceRootLeaseState::Released:
      return {};
    }
    return "neverc: error: cannot start linker time trace: managed profiler "
           "state is inconsistent";
  }

  llvm::Error write(llvm::StringRef OutputFile) const {
    return neverc::LLVMTimeTraceProfilerOwner::write(/*PreferredFile=*/{},
                                                      OutputFile);
  }
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
