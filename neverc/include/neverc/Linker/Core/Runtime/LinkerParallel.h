#ifndef LINKER_CORE_RUNTIME_LINKERPARALLEL_H
#define LINKER_CORE_RUNTIME_LINKERPARALLEL_H

#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/ThreadPool.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace linker {

template <typename T, typename = void>
struct HasRangeBegin : std::false_type {};

template <typename T>
struct HasRangeBegin<T, std::void_t<decltype(std::begin(std::declval<T &>()))>>
    : std::true_type {};

template <typename Function> auto bindLinkerContext(Function &&Fn) {
  CommonLinkerContext *Context = currentLinkerContext();
  auto Bound =
      std::make_shared<std::decay_t<Function>>(std::forward<Function>(Fn));
  return [Context, Bound](auto &&...Arguments) -> decltype(auto) {
    if (!Context)
      return std::invoke(*Bound,
                         std::forward<decltype(Arguments)>(Arguments)...);
    LinkerContextGuard Guard(*Context, Context->workerSlotForCurrentThread());
    return std::invoke(*Bound, std::forward<decltype(Arguments)>(Arguments)...);
  };
}

class LinkerTaskGroup {
public:
  explicit LinkerTaskGroup(
      neverc::ResourcePhase Phase = neverc::ResourcePhase::LinkParseResolve)
      : Context(currentLinkerContext()), Phase(Phase) {
    if (Context && Context->parallelPool())
      Group =
          std::make_unique<llvm::ThreadPoolTaskGroup>(*Context->parallelPool());
  }
  LinkerTaskGroup(const LinkerTaskGroup &) = delete;
  LinkerTaskGroup &operator=(const LinkerTaskGroup &) = delete;
  ~LinkerTaskGroup() { sync(); }

  template <typename Function>
  void spawn(Function &&Fn, bool Sequential = false) {
    auto Bound = bindLinkerContext(std::forward<Function>(Fn));
    if (!Group || Sequential) {
      Bound();
      return;
    }

    neverc::ProcessResourceBroker &Broker =
        neverc::ProcessResourceBroker::global();
    if (!Broker.enabled()) {
      Group->async(std::move(Bound));
      return;
    }

    // A pool worker must never enqueue into the same pool and then wait for
    // it: with a fully occupied pool that is a classic nested-task deadlock.
    if (currentLinkerWorkerSlot() != 0) {
      Bound();
      return;
    }

    neverc::ResourceWorkerGrant Grant = Broker.grantWorkers(
        Context->resourceSession(), Phase, /*DesiredWorkers=*/2);
    if (Grant.workerCount() < 2) {
      Bound();
      return;
    }

    auto GrantOwner =
        std::make_shared<neverc::ResourceWorkerGrant>(std::move(Grant));
    Group->async([Bound = std::move(Bound),
                  GrantOwner = std::move(GrantOwner)]() mutable {
      auto ReleaseGrant = llvm::make_scope_exit([&] { GrantOwner.reset(); });
      Bound();
    });
  }

  void sync() {
    if (Group)
      Group->wait();
  }
  bool isParallel() const { return Group != nullptr; }

private:
  CommonLinkerContext *Context = nullptr;
  neverc::ResourcePhase Phase;
  std::unique_ptr<llvm::ThreadPoolTaskGroup> Group;
};

inline unsigned parallelThreadCount() {
  CommonLinkerContext *Context = currentLinkerContext();
  return Context ? Context->parallelThreadCount() : 1;
}

/// Return the worker count for a compression library called from one already
/// broker-accounted linker task. Zstd treats zero as a different, synchronous
/// compression mode, so retain at least one asynchronous worker to preserve
/// the existing frame construction while preventing an outer-task x inner-pool
/// multiplication. The budget-disabled path is deliberately byte-for-byte
/// compatible with the previous logical thread-count choice.
inline unsigned nestedCompressionWorkerCount(unsigned DesiredWorkers) {
  if (!neverc::ProcessResourceBroker::global().enabled())
    return DesiredWorkers;
  return std::min(DesiredWorkers, 1U);
}

inline bool parallelEnabled() {
  CommonLinkerContext *Context = currentLinkerContext();
  return Context && Context->parallelEnabled();
}

template <typename Function>
void parallelForWithContext(size_t Begin, size_t End, Function &&Fn) {
  if (Begin == End)
    return;
  if (!parallelEnabled() || End - Begin == 1) {
    for (size_t Index = Begin; Index != End; ++Index)
      std::invoke(Fn, Index);
    return;
  }

  // Workers claim small grains of the range from a shared cursor, so items of
  // very different cost (such as one huge object file among small ones) do
  // not leave most workers idle behind one statically assigned chunk.
  struct SharedState {
    explicit SharedState(Function &&Fn, size_t Begin)
        : Fn(std::forward<Function>(Fn)), Next(Begin) {}
    std::decay_t<Function> Fn;
    std::atomic<size_t> Next;
  };
  auto State = std::make_shared<SharedState>(std::forward<Function>(Fn), Begin);
  const size_t ItemCount = End - Begin;
  const size_t Threads = parallelThreadCount();
  const size_t Grain = std::max<size_t>(1, ItemCount / (Threads * 32));
  const size_t Workers = std::min(Threads, (ItemCount + Grain - 1) / Grain);
  LinkerTaskGroup Group;
  for (size_t Worker = 0; Worker != Workers; ++Worker)
    Group.spawn([State, End, Grain] {
      for (;;) {
        const size_t TaskBegin =
            State->Next.fetch_add(Grain, std::memory_order_relaxed);
        if (TaskBegin >= End)
          return;
        const size_t TaskEnd = std::min(End, TaskBegin + Grain);
        for (size_t Index = TaskBegin; Index != TaskEnd; ++Index)
          std::invoke(State->Fn, Index);
      }
    });
}

template <typename Iterator, typename Function>
void parallelForEachWithContext(Iterator Begin, Iterator End, Function &&Fn) {
  auto FunctionOwner =
      std::make_shared<std::decay_t<Function>>(std::forward<Function>(Fn));
  parallelForWithContext(0, End - Begin, [Begin, FunctionOwner](size_t Index) {
    std::invoke(*FunctionOwner, Begin[Index]);
  });
}

template <typename Range, typename Function>
void parallelForEachWithContext(Range &&Values, Function &&Fn) {
  parallelForEachWithContext(std::begin(Values), std::end(Values),
                             std::forward<Function>(Fn));
}

/// Stable merge of two adjacent sorted runs into Out. Comparators receive
/// lvalues, as with std::sort, and ties take the left run first.
template <typename In, typename Out, typename Comparator>
void mergeSortedRuns(In Left, In LeftEnd, In Right, In RightEnd, Out Dest,
                     Comparator &Compare) {
  while (Left != LeftEnd && Right != RightEnd) {
    if (Compare(*Right, *Left))
      *Dest++ = std::move(*Right++);
    else
      *Dest++ = std::move(*Left++);
  }
  Dest = std::move(Left, LeftEnd, Dest);
  std::move(Right, RightEnd, Dest);
}

/// Sort [Begin, End) with the linker workers. Inputs of at least
/// ParallelSortMinimum elements always use a stable merge sort, run in parallel
/// when a worker pool exists, so the result never depends on the worker count.
/// Smaller inputs keep the previous llvm::sort behaviour.
template <typename Iterator, typename Comparator>
void parallelStableSortImpl(Iterator Begin, Iterator End, Comparator Compare) {
  constexpr size_t ParallelSortMinimum = size_t(1) << 15;
  const size_t Count = static_cast<size_t>(End - Begin);
  if (Count < ParallelSortMinimum) {
    llvm::sort(Begin, End, Compare);
    return;
  }
  if (!parallelEnabled()) {
    std::stable_sort(Begin, End, Compare);
    return;
  }

  using Value = typename std::iterator_traits<Iterator>::value_type;
  // A power-of-two chunk count keeps every merge round pairwise.
  size_t Chunks = 1;
  while (Chunks < static_cast<size_t>(parallelThreadCount()) * 2 &&
         Chunks * 4096 < Count)
    Chunks *= 2;
  const size_t Width = (Count + Chunks - 1) / Chunks;
  parallelForWithContext(0, Chunks, [&](size_t I) {
    const size_t From = std::min(Count, I * Width);
    const size_t To = std::min(Count, From + Width);
    std::stable_sort(Begin + From, Begin + To, Compare);
  });

  std::vector<Value> Scratch(std::make_move_iterator(Begin),
                             std::make_move_iterator(End));
  // Alternate between the scratch buffer and the original range. Each pair
  // of runs is cut along merge-path diagonals so that every round, including
  // the last single pair, keeps all workers busy. Ties take the left run
  // first, which keeps the merge stable.
  const size_t Pieces = Chunks;
  bool InScratch = true;
  for (size_t Run = Width; Run < Count; Run *= 2) {
    const size_t Pairs = (Count + 2 * Run - 1) / (2 * Run);
    const size_t PiecesPerPair = std::max<size_t>(1, Pieces / Pairs);
    auto MergeRound = [&](auto Src, auto Dst) {
      parallelForWithContext(0, Pairs * PiecesPerPair, [&](size_t Task) {
        const size_t P = Task / PiecesPerPair, K = Task % PiecesPerPair;
        const size_t Lo = P * 2 * Run;
        const size_t Mid = std::min(Count, Lo + Run);
        const size_t Hi = std::min(Count, Lo + 2 * Run);
        const size_t LeftSize = Mid - Lo, Total = Hi - Lo;
        // Number of left-run elements among the first D merged outputs.
        auto Split = [&](size_t D) {
          size_t L = D > Hi - Mid ? D - (Hi - Mid) : 0;
          size_t R = std::min(D, LeftSize);
          while (L < R) {
            const size_t M = L + (R - L) / 2;
            if (Compare(Src[Mid + (D - M - 1)], Src[Lo + M]))
              R = M;
            else
              L = M + 1;
          }
          return L;
        };
        const size_t D0 = Total * K / PiecesPerPair;
        const size_t D1 = Total * (K + 1) / PiecesPerPair;
        const size_t I0 = Split(D0), I1 = Split(D1);
        mergeSortedRuns(Src + Lo + I0, Src + Lo + I1, Src + Mid + (D0 - I0),
                        Src + Mid + (D1 - I1), Dst + Lo + D0, Compare);
      });
    };
    if (InScratch)
      MergeRound(Scratch.begin(), Begin);
    else
      MergeRound(Begin, Scratch.begin());
    InScratch = !InScratch;
  }
  if (InScratch)
    std::move(Scratch.begin(), Scratch.end(), Begin);
}

template <typename Range> void parallelSortWithContext(Range &&Values) {
  llvm::sort(std::begin(Values), std::end(Values));
}

template <typename First, typename Second>
void parallelSortWithContext(First &&FirstValue, Second &&SecondValue) {
  if constexpr (HasRangeBegin<std::decay_t<First>>::value) {
    using Element =
        decltype(*std::begin(std::declval<std::decay_t<First> &>()));
    if constexpr (std::is_invocable_v<std::decay_t<Second>, Element, Element>) {
      llvm::sort(std::begin(FirstValue), std::end(FirstValue),
                 std::forward<Second>(SecondValue));
    } else {
      llvm::sort(std::forward<First>(FirstValue),
                 std::forward<Second>(SecondValue));
    }
  } else {
    llvm::sort(std::forward<First>(FirstValue),
               std::forward<Second>(SecondValue));
  }
}

template <typename Iterator, typename Comparator>
void parallelSortWithContext(Iterator Begin, Iterator End,
                             Comparator &&Compare) {
  llvm::sort(Begin, End, std::forward<Comparator>(Compare));
}

/// Stable sort using the linker workers. Unlike parallelSort, which keeps
/// llvm::sort's tie order, equal elements keep their input order, so callers
/// whose comparator has ties get a different (but worker-count independent)
/// order than llvm::sort would produce.
template <typename Iterator, typename Comparator>
void parallelStableSort(Iterator Begin, Iterator End, Comparator Compare) {
  parallelStableSortImpl(Begin, End, Compare);
}

} // namespace linker

#define parallelFor(...) ::linker::parallelForWithContext(__VA_ARGS__)
#define parallelForEach(...) ::linker::parallelForEachWithContext(__VA_ARGS__)
#define parallelSort(...) ::linker::parallelSortWithContext(__VA_ARGS__)

#endif
