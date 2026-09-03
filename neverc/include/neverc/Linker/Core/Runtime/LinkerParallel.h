#ifndef LINKER_CORE_RUNTIME_LINKERPARALLEL_H
#define LINKER_CORE_RUNTIME_LINKERPARALLEL_H

#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/ThreadPool.h"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

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

  auto FunctionOwner =
      std::make_shared<std::decay_t<Function>>(std::forward<Function>(Fn));
  const size_t ItemCount = End - Begin;
  const size_t TaskCount =
      std::min(ItemCount, static_cast<size_t>(parallelThreadCount()) * 4);
  const size_t TaskSize = (ItemCount + TaskCount - 1) / TaskCount;
  LinkerTaskGroup Group;
  for (size_t TaskBegin = Begin; TaskBegin < End; TaskBegin += TaskSize) {
    const size_t TaskEnd = std::min(End, TaskBegin + TaskSize);
    Group.spawn([FunctionOwner, TaskBegin, TaskEnd] {
      for (size_t Index = TaskBegin; Index != TaskEnd; ++Index)
        std::invoke(*FunctionOwner, Index);
    });
  }
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

namespace detail {

constexpr std::ptrdiff_t MinParallelSortSize = 1024;

template <typename RandomAccessIterator, typename Comparator>
RandomAccessIterator medianOfThree(RandomAccessIterator Begin,
                                   RandomAccessIterator End,
                                   const Comparator &Compare) {
  RandomAccessIterator Middle = Begin + std::distance(Begin, End) / 2;
  return Compare(*Begin, *(End - 1))
             ? (Compare(*Middle, *(End - 1))
                    ? (Compare(*Begin, *Middle) ? Middle : Begin)
                    : End - 1)
             : (Compare(*Middle, *Begin)
                    ? (Compare(*(End - 1), *Middle) ? Middle : End - 1)
                    : Begin);
}

template <typename RandomAccessIterator, typename Comparator>
void parallelQuickSort(RandomAccessIterator Begin, RandomAccessIterator End,
                       const Comparator &Compare, LinkerTaskGroup &Group,
                       size_t Depth) {
  if (std::distance(Begin, End) < MinParallelSortSize || Depth == 0) {
    llvm::sort(Begin, End, Compare);
    return;
  }

  RandomAccessIterator Pivot = medianOfThree(Begin, End, Compare);
  std::swap(*(End - 1), *Pivot);
  Pivot = std::partition(Begin, End - 1,
                         [&Compare, End](decltype(*Begin) Value) {
                           return Compare(Value, *(End - 1));
                         });
  std::swap(*Pivot, *(End - 1));

  Group.spawn([Begin, Pivot, &Compare, &Group, Depth] {
    parallelQuickSort(Begin, Pivot, Compare, Group, Depth - 1);
  });
  parallelQuickSort(Pivot + 1, End, Compare, Group, Depth - 1);
}

template <typename RandomAccessIterator, typename Comparator>
void parallelSort(RandomAccessIterator Begin, RandomAccessIterator End,
                  Comparator &&Compare) {
  const auto ItemCount = std::distance(Begin, End);
  if (ItemCount < MinParallelSortSize || !parallelEnabled() ||
      currentLinkerWorkerSlot() != 0) {
    llvm::sort(Begin, End, std::forward<Comparator>(Compare));
    return;
  }

  std::decay_t<Comparator> CompareOwner(std::forward<Comparator>(Compare));
  LinkerTaskGroup Group;
  if (!Group.isParallel()) {
    llvm::sort(Begin, End, CompareOwner);
    return;
  }

  size_t Depth = 0;
  for (auto Remaining = ItemCount; Remaining > 1; Remaining >>= 1)
    ++Depth;
  parallelQuickSort(Begin, End, CompareOwner, Group, Depth);
  Group.sync();
}

} // namespace detail

template <typename Range> void parallelSortWithContext(Range &&Values) {
  using Iterator = decltype(std::begin(Values));
  using Value = typename std::iterator_traits<Iterator>::value_type;
  detail::parallelSort(std::begin(Values), std::end(Values), std::less<Value>());
}

template <typename First, typename Second>
void parallelSortWithContext(First &&FirstValue, Second &&SecondValue) {
  if constexpr (HasRangeBegin<std::decay_t<First>>::value) {
    using Element =
        decltype(*std::begin(std::declval<std::decay_t<First> &>()));
    if constexpr (std::is_invocable_v<std::decay_t<Second>, Element, Element>) {
      detail::parallelSort(std::begin(FirstValue), std::end(FirstValue),
                           std::forward<Second>(SecondValue));
    } else {
      llvm::sort(std::forward<First>(FirstValue),
                 std::forward<Second>(SecondValue));
    }
  } else {
    using Iterator = std::decay_t<First>;
    using Value = typename std::iterator_traits<Iterator>::value_type;
    detail::parallelSort(std::forward<First>(FirstValue),
                         std::forward<Second>(SecondValue),
                         std::less<Value>());
  }
}

template <typename Iterator, typename Comparator>
void parallelSortWithContext(Iterator Begin, Iterator End,
                             Comparator &&Compare) {
  detail::parallelSort(Begin, End, std::forward<Comparator>(Compare));
}

} // namespace linker

#define parallelFor(...) ::linker::parallelForWithContext(__VA_ARGS__)
#define parallelForEach(...) ::linker::parallelForEachWithContext(__VA_ARGS__)
#define parallelSort(...) ::linker::parallelSortWithContext(__VA_ARGS__)

#endif
