#ifndef LINKER_CORE_RUNTIME_LINKERPARALLEL_H
#define LINKER_CORE_RUNTIME_LINKERPARALLEL_H

#include "Linker/Core/Runtime/Session.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/ThreadPool.h"
#include <algorithm>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace linker {

template <typename T, typename = void>
struct HasRangeBegin : std::false_type {};

template <typename T>
struct HasRangeBegin<
    T, std::void_t<decltype(std::begin(std::declval<T &>()))>>
    : std::true_type {};

template <typename Function>
auto bindLinkerContext(Function &&Fn) {
  CommonLinkerContext *Context = currentLinkerContext();
  auto Bound =
      std::make_shared<std::decay_t<Function>>(std::forward<Function>(Fn));
  return [Context, Bound](
             auto &&...Arguments) -> decltype(auto) {
    if (!Context)
      return std::invoke(*Bound,
                         std::forward<decltype(Arguments)>(Arguments)...);
    LinkerContextGuard Guard(*Context,
                             Context->workerSlotForCurrentThread());
    return std::invoke(*Bound,
                       std::forward<decltype(Arguments)>(Arguments)...);
  };
}

class LinkerTaskGroup {
public:
  LinkerTaskGroup() : Context(currentLinkerContext()) {
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
    Group->async(std::move(Bound));
  }

  void sync() {
    if (Group)
      Group->wait();
  }
  bool isParallel() const { return Group != nullptr; }

private:
  CommonLinkerContext *Context = nullptr;
  std::unique_ptr<llvm::ThreadPoolTaskGroup> Group;
};

inline unsigned parallelThreadCount() {
  CommonLinkerContext *Context = currentLinkerContext();
  return Context ? Context->parallelThreadCount() : 1;
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

template <typename Range>
void parallelSortWithContext(Range &&Values) {
  llvm::sort(std::begin(Values), std::end(Values));
}

template <typename First, typename Second>
void parallelSortWithContext(First &&FirstValue, Second &&SecondValue) {
  if constexpr (HasRangeBegin<std::decay_t<First>>::value) {
    using Element =
        decltype(*std::begin(std::declval<std::decay_t<First> &>()));
    if constexpr (std::is_invocable_v<std::decay_t<Second>, Element,
                                      Element>) {
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

} // namespace linker

#define parallelFor(...)                                                      \
  ::linker::parallelForWithContext(__VA_ARGS__)
#define parallelForEach(...)                                                  \
  ::linker::parallelForEachWithContext(__VA_ARGS__)
#define parallelSort(...)                                                     \
  ::linker::parallelSortWithContext(__VA_ARGS__)

#endif
