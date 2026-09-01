#ifndef NEVERC_FOUNDATION_CORE_LLVMTIMETRACEROOTLEASE_H
#define NEVERC_FOUNDATION_CORE_LLVMTIMETRACEROOTLEASE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TimeProfiler.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace llvm {
class raw_fd_ostream;
}

namespace neverc {

/// Result of trying to join NeverC's process-wide LLVM time-trace root.
enum class LLVMTimeTraceRootLeaseState : std::uint8_t {
  Owned,
  Borrowed,
  Busy,
  UnmanagedAmbient,
  Inconsistent,
  Released,
};

namespace time_trace_detail {

/// Identity captured by each participant in one managed root.
/// The generation token prevents an abandoned crash cleanup from touching a
/// later profiler that happens to reuse the same address.
struct RootGenerationBinding {
  std::uint64_t Token = 0;
  llvm::TimeTraceProfiler *Profiler = nullptr;
};

void invalidateProfilerAfterCrash(
    const RootGenerationBinding &Binding) noexcept;

/// Test-only visibility into the current thread's managed generation. This
/// exposes identity, not ownership; callers cannot mutate the managed state.
RootGenerationBinding captureCurrentRootBindingForTesting() noexcept;

/// Returns true only when the current thread is participating in a healthy
/// NeverC-managed root and an active crash-recovery context could abandon a
/// time-trace scope. This is a read-only guard predicate: it never claims a
/// root, initializes a profiler, or observes another thread's generation.
bool shouldGuardManagedRootAgainstCrash() noexcept;

} // namespace time_trace_detail

/// A non-blocking process-wide lease for LLVM's root time-trace profiler.
///
/// LLVM keeps the current profiler in TLS and tags every live and finished
/// worker with this lease's nonzero generation token. Same-thread nesting
/// borrows the active root; a second root on another thread fails closed.
///
/// The lease must be released on the acquiring thread. The LLVM profiler TLS
/// has the same requirement, so cross-thread destruction and
/// CrashRecoveryContext::RunSafelyOnThread are intentionally unsupported. A
/// lease created inside RunSafely must also remain an automatic object in that
/// protected callback; storing it in an outer frame would outlive the recovery
/// context's heap cleanup. Every worker contributing to this root must inherit
/// the captured session and finish before the owner writes; a checked write
/// returns an Error while workers remain active. A late worker from a closed
/// generation self-discards instead of entering a later root's trace.
class LLVMTimeTraceRootLease {
public:
  LLVMTimeTraceRootLease();
  ~LLVMTimeTraceRootLease();

  LLVMTimeTraceRootLease(const LLVMTimeTraceRootLease &) = delete;
  LLVMTimeTraceRootLease &operator=(const LLVMTimeTraceRootLease &) = delete;
  LLVMTimeTraceRootLease(LLVMTimeTraceRootLease &&) = delete;
  LLVMTimeTraceRootLease &operator=(LLVMTimeTraceRootLease &&) = delete;

  LLVMTimeTraceRootLeaseState state() const noexcept;
  bool ownsRoot() const noexcept {
    return state() == LLVMTimeTraceRootLeaseState::Owned;
  }
  bool borrowsRoot() const noexcept {
    return state() == LLVMTimeTraceRootLeaseState::Borrowed;
  }

private:
  friend class LLVMTimeTraceProfilerOwner;

  /// One depth-counted participant in a managed root generation. The public
  /// lease keeps this inline outside crash recovery. Under an active recovery
  /// context it is heap-owned and registered exactly once, because longjmp
  /// abandons the public lease's stack frame without running its destructor.
  struct Participant {
    Participant() = default;
    ~Participant();

    Participant(const Participant &) = delete;
    Participant &operator=(const Participant &) = delete;
    Participant(Participant &&) = delete;
    Participant &operator=(Participant &&) = delete;

    void acquire();
    /// Idempotent for every acquisition result. An owned participant releases
    /// only after the last same-generation participant has left and the LLVM
    /// root profiler and worker registry have been cleaned.
    void release() noexcept;
    bool bindProfiler(llvm::TimeTraceProfiler *Profiler) noexcept;
    llvm::StringRef
    validateProfiler(const llvm::TimeTraceProfiler *Profiler) const noexcept;
    llvm::StringRef validateProfilerForWrite(
        const llvm::TimeTraceProfiler *Profiler) const noexcept;

    LLVMTimeTraceRootLeaseState State =
        LLVMTimeTraceRootLeaseState::Released;
    std::thread::id AcquiringThread;
    time_trace_detail::RootGenerationBinding Binding;
  };

  bool bindProfiler(llvm::TimeTraceProfiler *Profiler) noexcept;
  llvm::StringRef
  validateProfiler(const llvm::TimeTraceProfiler *Profiler) const noexcept;
  llvm::StringRef validateProfilerForWrite(
      const llvm::TimeTraceProfiler *Profiler) const noexcept;
  time_trace_detail::RootGenerationBinding &binding() noexcept {
    return Active->Binding;
  }
  const time_trace_detail::RootGenerationBinding &binding() const noexcept {
    return Active->Binding;
  }

  std::optional<Participant> LocalParticipant;
  std::unique_ptr<Participant> CrashParticipant;
  Participant *Active = nullptr;
  std::optional<llvm::CrashRecoveryContextCleanupRegistrar<Participant>>
      ParticipantCleanup;
};

llvm::StringRef
describeLLVMTimeTraceRootLeaseState(LLVMTimeTraceRootLeaseState State);

namespace time_trace_detail {

class ProfilerCrashCleanup final
    : public llvm::CrashRecoveryContextCleanupBase<
          ProfilerCrashCleanup, RootGenerationBinding> {
public:
  ProfilerCrashCleanup(llvm::CrashRecoveryContext *Context,
                       RootGenerationBinding *Binding)
      : llvm::CrashRecoveryContextCleanupBase<ProfilerCrashCleanup,
                                              RootGenerationBinding>(
            Context, Binding) {}

  void recoverResources() override {
    invalidateProfilerAfterCrash(*this->resource);
  }
};

} // namespace time_trace_detail

/// Owns the LLVM profiler paired with a process-wide root lease. Same-thread
/// nested users borrow the active profiler and never write or clean it on a
/// normal return. If a nested recovery context skips trace-scope destructors,
/// its crash cleanup invalidates the shared profiler so the outer owner fails
/// diagnostically instead of asserting while writing a poisoned scope stack.
/// Like its lease, an owner created inside RunSafely must not escape that
/// protected callback.
class LLVMTimeTraceProfilerOwner {
public:
  LLVMTimeTraceProfilerOwner(unsigned Granularity,
                             llvm::StringRef ProcessName);
  ~LLVMTimeTraceProfilerOwner();

  LLVMTimeTraceProfilerOwner(const LLVMTimeTraceProfilerOwner &) = delete;
  LLVMTimeTraceProfilerOwner &
  operator=(const LLVMTimeTraceProfilerOwner &) = delete;
  LLVMTimeTraceProfilerOwner(LLVMTimeTraceProfilerOwner &&) = delete;
  LLVMTimeTraceProfilerOwner &operator=(LLVMTimeTraceProfilerOwner &&) =
      delete;

  LLVMTimeTraceRootLeaseState state() const noexcept {
    return Lease.state();
  }
  bool ownsProfiler() const noexcept;
  llvm::StringRef acquisitionError() const;

  /// The generic stream contract validates and flushes the managed profiler,
  /// but raw_pwrite_stream exposes no portable I/O-error query. Callers that
  /// own an fd stream must retain its raw_fd_ostream static type so the typed
  /// overload can capture and clear errors before the stream is destroyed.
  llvm::Error write(llvm::raw_pwrite_stream &Output) const;
  llvm::Error write(llvm::raw_fd_ostream &Output) const;
  llvm::Error write(llvm::StringRef PreferredFile,
                    llvm::StringRef FallbackFile) const;

private:
  LLVMTimeTraceRootLease Lease;
  llvm::TimeTraceProfiler *Profiler = nullptr;
  std::string AcquisitionFailure;
  std::optional<llvm::CrashRecoveryContextCleanupRegistrar<
      time_trace_detail::RootGenerationBinding,
      time_trace_detail::ProfilerCrashCleanup>>
      ProfilerCleanup;
};

} // namespace neverc

#endif // NEVERC_FOUNDATION_CORE_LLVMTIMETRACEROOTLEASE_H
