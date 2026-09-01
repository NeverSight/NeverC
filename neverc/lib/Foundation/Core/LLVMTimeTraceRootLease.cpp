#include "neverc/Foundation/Core/LLVMTimeTraceRootLease.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"

#include <atomic>
#include <limits>

namespace neverc::time_trace_detail {

struct ManagedRootThreadState {
  std::uint64_t Token = 0;
  unsigned Depth = 0;
  llvm::TimeTraceProfiler *Profiler = nullptr;
  bool Closing = false;
  bool Invalidated = false;
};

// Even values mean idle and odd values identify the one active generation.
// Advancing the token on every release makes a stale crash cleanup harmless
// even if LLVM later allocates a profiler at the same address.
std::atomic<std::uint64_t> LLVMTimeTraceRootGeneration{0};

// Reserve the last even value as a permanently exhausted idle state. Never
// issuing the following odd token prevents generation wraparound from making
// an abandoned cleanup look current again after profiler-address reuse.
constexpr std::uint64_t ExhaustedRootGeneration =
    std::numeric_limits<std::uint64_t>::max() - 1;
static_assert((ExhaustedRootGeneration & 1U) == 0);

// Same-thread participation and the profiler identity needed to validate
// nested borrowers. This is ownership metadata only; it stores no path,
// option, invocation, graph, symbol, output, or cache payload.
thread_local ManagedRootThreadState ManagedLLVMTimeTraceRootState;

static bool generationMatches(const RootGenerationBinding &Binding) {
  return Binding.Token != 0 && (Binding.Token & 1U) != 0 &&
         ManagedLLVMTimeTraceRootState.Token == Binding.Token &&
         ManagedLLVMTimeTraceRootState.Depth != 0 &&
         LLVMTimeTraceRootGeneration.load(std::memory_order_acquire) ==
             Binding.Token;
}

void invalidateProfilerAfterCrash(
    const RootGenerationBinding &Binding) noexcept {
  if (!generationMatches(Binding) || !Binding.Profiler ||
      ManagedLLVMTimeTraceRootState.Profiler != Binding.Profiler)
    return;

  ManagedLLVMTimeTraceRootState.Invalidated = true;
  llvm::timeTraceProfilerCleanupSession(Binding.Token);
}

RootGenerationBinding captureCurrentRootBindingForTesting() noexcept {
  return {ManagedLLVMTimeTraceRootState.Token,
          ManagedLLVMTimeTraceRootState.Profiler};
}

bool shouldGuardManagedRootAgainstCrash() noexcept {
  if (!llvm::CrashRecoveryContext::GetCurrent())
    return false;

  const auto &ThreadState = ManagedLLVMTimeTraceRootState;
  const RootGenerationBinding Binding{ThreadState.Token,
                                      ThreadState.Profiler};
  return generationMatches(Binding) && !ThreadState.Closing &&
         !ThreadState.Invalidated && Binding.Profiler &&
         llvm::getTimeTraceProfilerInstance() == Binding.Profiler &&
         llvm::timeTraceProfilerCurrentSession() == Binding.Token;
}

} // namespace neverc::time_trace_detail

namespace neverc {

void LLVMTimeTraceRootLease::Participant::acquire() {
  llvm::TimeTraceProfiler *Existing = llvm::getTimeTraceProfilerInstance();
  auto &ThreadState = time_trace_detail::ManagedLLVMTimeTraceRootState;
  if (ThreadState.Depth != 0) {
    Binding = {ThreadState.Token, ThreadState.Profiler};
    if (!time_trace_detail::generationMatches(Binding) ||
        ThreadState.Closing || ThreadState.Invalidated || !Existing ||
        Existing != ThreadState.Profiler) {
      State = LLVMTimeTraceRootLeaseState::Inconsistent;
      return;
    }
    if (ThreadState.Depth == std::numeric_limits<unsigned>::max()) {
      State = LLVMTimeTraceRootLeaseState::Inconsistent;
      return;
    }
    ++ThreadState.Depth;
    State = LLVMTimeTraceRootLeaseState::Borrowed;
    AcquiringThread = std::this_thread::get_id();
    return;
  }

  if (Existing) {
    State = LLVMTimeTraceRootLeaseState::UnmanagedAmbient;
    return;
  }

  std::uint64_t Observed =
      time_trace_detail::LLVMTimeTraceRootGeneration.load(
          std::memory_order_acquire);
  for (;;) {
    if (Observed == time_trace_detail::ExhaustedRootGeneration) {
      State = LLVMTimeTraceRootLeaseState::Inconsistent;
      return;
    }
    if ((Observed & 1U) != 0) {
      State = LLVMTimeTraceRootLeaseState::Busy;
      return;
    }
    const std::uint64_t ActiveToken = Observed + 1;
    if (time_trace_detail::LLVMTimeTraceRootGeneration.compare_exchange_strong(
            Observed, ActiveToken, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      ThreadState = {};
      ThreadState.Token = ActiveToken;
      ThreadState.Depth = 1;
      Binding.Token = ActiveToken;
      break;
    }
  }

  State = LLVMTimeTraceRootLeaseState::Owned;
  AcquiringThread = std::this_thread::get_id();
}

LLVMTimeTraceRootLease::Participant::~Participant() {
  // A raw lease has no separate profiler cleanup. Conservatively invalidate
  // its generation when crash recovery deletes the participant: the skipped
  // frame may have abandoned a TimeTraceScope. ProfilerOwner registers the
  // same invalidation earlier in the cleanup LIFO; repeating it is idempotent.
  if (llvm::CrashRecoveryContext::isRecoveringFromCrash())
    time_trace_detail::invalidateProfilerAfterCrash(Binding);
  release();
}

void LLVMTimeTraceRootLease::Participant::release() noexcept {
  if (State != LLVMTimeTraceRootLeaseState::Owned &&
      State != LLVMTimeTraceRootLeaseState::Borrowed) {
    State = LLVMTimeTraceRootLeaseState::Released;
    return;
  }
  if (AcquiringThread != std::this_thread::get_id())
    llvm::report_fatal_error(
        "LLVM time-trace root lease released on a different thread");

  const LLVMTimeTraceRootLeaseState PriorState = State;
  State = LLVMTimeTraceRootLeaseState::Released;

  auto &ThreadState = time_trace_detail::ManagedLLVMTimeTraceRootState;
  if (!time_trace_detail::generationMatches(Binding))
    return;

  if (PriorState == LLVMTimeTraceRootLeaseState::Owned)
    ThreadState.Closing = true;
  --ThreadState.Depth;
  if (ThreadState.Depth != 0)
    return;

  // The owner is always one participant, so a well-formed generation can
  // reach zero only after it has entered the closing state.
  ThreadState.Closing = true;
  // Session cleanup is exact and idempotent. Active same-session workers keep
  // their own profiler until finish, then self-discard because this root is
  // closed. A foreign legacy/raw profiler and every other session survive.
  if (Binding.Token != 0 &&
      (ThreadState.Profiler ||
       llvm::timeTraceProfilerCurrentSession() == Binding.Token))
    llvm::timeTraceProfilerCleanupSession(Binding.Token);
  else if (!ThreadState.Profiler &&
           llvm::timeTraceProfilerCurrentSession() == 0 &&
           llvm::getTimeTraceProfilerInstance())
    llvm::timeTraceProfilerCleanupCurrentThread();

  const std::uint64_t Token = Binding.Token;
  ThreadState = {};
  std::uint64_t Expected = Token;
  const std::uint64_t ReleasedToken =
      Token >= time_trace_detail::ExhaustedRootGeneration
          ? time_trace_detail::ExhaustedRootGeneration
          : Token + 1;
  if (!time_trace_detail::LLVMTimeTraceRootGeneration.compare_exchange_strong(
          Expected, ReleasedToken, std::memory_order_release,
          std::memory_order_relaxed))
    llvm::report_fatal_error(
        "LLVM time-trace root generation changed before final release");
}

bool LLVMTimeTraceRootLease::Participant::bindProfiler(
    llvm::TimeTraceProfiler *Profiler) noexcept {
  auto &ThreadState = time_trace_detail::ManagedLLVMTimeTraceRootState;
  if (State != LLVMTimeTraceRootLeaseState::Owned || !Profiler ||
      AcquiringThread != std::this_thread::get_id() ||
      !time_trace_detail::generationMatches(Binding) || ThreadState.Closing ||
      ThreadState.Invalidated || ThreadState.Profiler ||
      llvm::getTimeTraceProfilerInstance() != Profiler ||
      llvm::timeTraceProfilerCurrentSession() != Binding.Token)
    return false;
  ThreadState.Profiler = Profiler;
  Binding.Profiler = Profiler;
  return true;
}

llvm::StringRef LLVMTimeTraceRootLease::Participant::validateProfiler(
    const llvm::TimeTraceProfiler *Profiler) const noexcept {
  if (State != LLVMTimeTraceRootLeaseState::Owned &&
      State != LLVMTimeTraceRootLeaseState::Borrowed)
    return "LLVM time-trace root lease is no longer active";
  if (AcquiringThread != std::this_thread::get_id())
    return "LLVM time-trace root lease used on a different thread";
  if (!time_trace_detail::generationMatches(Binding))
    return "LLVM time-trace root generation changed before write";

  const auto &ThreadState =
      time_trace_detail::ManagedLLVMTimeTraceRootState;
  if (ThreadState.Invalidated)
    return "LLVM time-trace profiler was invalidated during crash recovery";
  if (!Profiler || Binding.Profiler != Profiler ||
      ThreadState.Profiler != Profiler ||
      llvm::getTimeTraceProfilerInstance() != Profiler ||
      llvm::timeTraceProfilerCurrentSession() != Binding.Token)
    return "LLVM time-trace profiler ownership changed before write";
  return {};
}

llvm::StringRef LLVMTimeTraceRootLease::Participant::validateProfilerForWrite(
    const llvm::TimeTraceProfiler *Profiler) const noexcept {
  if (llvm::StringRef Error = validateProfiler(Profiler); !Error.empty())
    return Error;
  if (State == LLVMTimeTraceRootLeaseState::Owned) {
    if (!Profiler->Stack.empty())
      return "LLVM time-trace root still has active scopes";
    if (time_trace_detail::ManagedLLVMTimeTraceRootState.Depth != 1)
      return "LLVM time-trace root still has active nested participants";
  }
  return {};
}

LLVMTimeTraceRootLease::LLVMTimeTraceRootLease() {
  if (llvm::CrashRecoveryContext::GetCurrent()) {
    CrashParticipant = std::make_unique<Participant>();
    Active = CrashParticipant.get();
    ParticipantCleanup.emplace(Active);
    Active->acquire();
    return;
  }

  LocalParticipant.emplace();
  Active = &*LocalParticipant;
  Active->acquire();
}

LLVMTimeTraceRootLease::~LLVMTimeTraceRootLease() {
  // The normal path must unregister before deleting the resource. On a fatal
  // path this frame is abandoned and the recovery context's delete cleanup is
  // the sole owner of the heap participant.
  ParticipantCleanup.reset();
  CrashParticipant.reset();
  LocalParticipant.reset();
  Active = nullptr;
}

LLVMTimeTraceRootLeaseState LLVMTimeTraceRootLease::state() const noexcept {
  return Active ? Active->State : LLVMTimeTraceRootLeaseState::Released;
}

bool LLVMTimeTraceRootLease::bindProfiler(
    llvm::TimeTraceProfiler *Profiler) noexcept {
  return Active && Active->bindProfiler(Profiler);
}

llvm::StringRef LLVMTimeTraceRootLease::validateProfiler(
    const llvm::TimeTraceProfiler *Profiler) const noexcept {
  if (!Active)
    return "LLVM time-trace root lease is no longer active";
  return Active->validateProfiler(Profiler);
}

llvm::StringRef LLVMTimeTraceRootLease::validateProfilerForWrite(
    const llvm::TimeTraceProfiler *Profiler) const noexcept {
  if (!Active)
    return "LLVM time-trace root lease is no longer active";
  return Active->validateProfilerForWrite(Profiler);
}

llvm::StringRef
describeLLVMTimeTraceRootLeaseState(LLVMTimeTraceRootLeaseState State) {
  switch (State) {
  case LLVMTimeTraceRootLeaseState::Busy:
    return "another LLVM time-trace root is already active";
  case LLVMTimeTraceRootLeaseState::UnmanagedAmbient:
    return "the current thread has an unmanaged LLVM time-trace profiler";
  case LLVMTimeTraceRootLeaseState::Inconsistent:
    return "managed LLVM time-trace state is inconsistent";
  case LLVMTimeTraceRootLeaseState::Owned:
  case LLVMTimeTraceRootLeaseState::Borrowed:
  case LLVMTimeTraceRootLeaseState::Released:
    return {};
  }
  return "managed LLVM time-trace state is inconsistent";
}

LLVMTimeTraceProfilerOwner::LLVMTimeTraceProfilerOwner(
    unsigned Granularity, llvm::StringRef ProcessName) {
  if (Lease.borrowsRoot()) {
    // A nested recovery context can abandon TimeTraceScope destructors and
    // poison the outer profiler's stack. On that fatal path, invalidate the
    // shared profiler instead of letting the outer owner assert while writing.
    Profiler = Lease.binding().Profiler;
    ProfilerCleanup.emplace(&Lease.binding());
    return;
  }

  if (!Lease.ownsRoot())
    return;

  // The lease registered its participant cleanup during member construction.
  // Register the profiler second so crash recovery invalidates and cleans it
  // before the participant's LIFO cleanup releases the shared registry.
  if (llvm::Error Error = llvm::timeTraceProfilerInitializeSession(
          Granularity, ProcessName, Lease.binding().Token)) {
    AcquisitionFailure = llvm::toString(std::move(Error)).str().str();
    return;
  }
  Profiler = llvm::getTimeTraceProfilerInstance();
  if (!Lease.bindProfiler(Profiler)) {
    llvm::timeTraceProfilerCleanupSession(Lease.binding().Token);
    Profiler = nullptr;
    AcquisitionFailure =
        "managed LLVM time-trace profiler could not be bound to its session";
    return;
  }
  ProfilerCleanup.emplace(&Lease.binding());
}

LLVMTimeTraceProfilerOwner::~LLVMTimeTraceProfilerOwner() {
  ProfilerCleanup.reset();
  Profiler = nullptr;
}

bool LLVMTimeTraceProfilerOwner::ownsProfiler() const noexcept {
  return Lease.ownsRoot() && Lease.validateProfiler(Profiler).empty();
}

llvm::StringRef LLVMTimeTraceProfilerOwner::acquisitionError() const {
  if (!AcquisitionFailure.empty())
    return AcquisitionFailure;
  if (Lease.ownsRoot() && !Profiler)
    return "managed LLVM time-trace profiler could not be initialized";
  return describeLLVMTimeTraceRootLeaseState(state());
}

llvm::Error
LLVMTimeTraceProfilerOwner::write(llvm::raw_pwrite_stream &Output) const {
  if (llvm::StringRef Error = acquisitionError(); !Error.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(), Error);
  if (llvm::StringRef Error = Lease.validateProfilerForWrite(Profiler);
      !Error.empty())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(), Error);
  if (Lease.borrowsRoot())
    return llvm::Error::success();
  if (llvm::Error Error = llvm::timeTraceProfilerWriteSession(
          Output, Lease.binding().Token))
    return Error;
  Output.flush();
  return llvm::Error::success();
}

llvm::Error
LLVMTimeTraceProfilerOwner::write(llvm::raw_fd_ostream &Output) const {
  if (llvm::Error Error =
          write(static_cast<llvm::raw_pwrite_stream &>(Output)))
    return Error;
  if (!Output.has_error())
    return llvm::Error::success();

  const std::error_code Error = Output.error();
  Output.clear_error();
  return llvm::createStringError(
      Error, llvm::Twine("Could not write LLVM time trace: ") +
                 Error.message());
}

llvm::Error
LLVMTimeTraceProfilerOwner::write(llvm::StringRef PreferredFile,
                                  llvm::StringRef FallbackFile) const {
  if (llvm::StringRef Error = acquisitionError(); !Error.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(), Error);
  if (llvm::StringRef Error = Lease.validateProfilerForWrite(Profiler);
      !Error.empty())
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(), Error);
  if (Lease.borrowsRoot())
    return llvm::Error::success();

  // Complete registry validation and render into private storage before
  // opening the destination. An active worker or other session error must not
  // create or truncate a trace path. The actual file write is therefore also
  // outside LLVM's profiler registry lock.
  llvm::SmallVector<char, 0> Bytes;
  llvm::raw_svector_ostream RenderedOutput(Bytes);
  if (llvm::Error Error = write(RenderedOutput))
    return Error;

  llvm::SmallString<256> Path(PreferredFile);
  if (Path.empty()) {
    Path = FallbackFile == "-" ? "out" : FallbackFile;
    Path += ".time-trace";
  }
  std::error_code OpenError;
  llvm::raw_fd_ostream Output(Path, OpenError,
                              llvm::sys::fs::OF_TextWithCRLF);
  if (OpenError)
    return llvm::createStringError(
        OpenError, llvm::Twine("Could not open ") + Path + ": " +
                       OpenError.message());

  Output.write(Bytes.data(), Bytes.size());
  if (Path == "-")
    Output.flush();
  else
    Output.close();
  if (Output.has_error()) {
    const std::error_code WriteError = Output.error();
    Output.clear_error();
    return llvm::createStringError(
        WriteError, llvm::Twine("Could not write LLVM time trace ") + Path +
                        ": " + WriteError.message());
  }
  return llvm::Error::success();
}

} // namespace neverc
