#ifndef NEVERC_FOUNDATION_CORE_THREADLOCALSTORAGE_H
#define NEVERC_FOUNDATION_CORE_THREADLOCALSTORAGE_H

#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace neverc {

/// Storage for dynamically scoped per-thread state. The holder has trivial
/// teardown; callers must reset the contained value before leaving its scope.
template <typename T> class ScopedThreadLocalValue {
public:
  ScopedThreadLocalValue() noexcept = default;
  ScopedThreadLocalValue(const ScopedThreadLocalValue &) = delete;
  ScopedThreadLocalValue &operator=(const ScopedThreadLocalValue &) = delete;

  bool hasValue() const noexcept { return Engaged; }

  T &get() noexcept {
    assert(Engaged && "accessing an empty thread-local value");
    return *valuePointer();
  }

  const T &get() const noexcept {
    assert(Engaged && "accessing an empty thread-local value");
    return *valuePointer();
  }

  template <typename... Args> T &emplace(Args &&...Arguments) {
    reset();
    T *Value = ::new (static_cast<void *>(&Storage))
        T(std::forward<Args>(Arguments)...);
    Engaged = true;
    return *Value;
  }

  template <typename U> T &set(U &&NewValue) {
    if (!Engaged)
      return emplace(std::forward<U>(NewValue));
    get() = std::forward<U>(NewValue);
    return get();
  }

  void reset() noexcept(std::is_nothrow_destructible_v<T>) {
    if (!Engaged)
      return;
    Engaged = false;
    valuePointer()->~T();
  }

private:
  using StorageType = std::aligned_storage_t<sizeof(T), alignof(T)>;

  T *valuePointer() noexcept {
    return std::launder(reinterpret_cast<T *>(&Storage));
  }

  const T *valuePointer() const noexcept {
    return std::launder(reinterpret_cast<const T *>(&Storage));
  }

  StorageType Storage;
  bool Engaged = false;
};

/// A small stack for dynamically scoped per-thread state. The holder has
/// trivial teardown; callers must balance every insertion with pop_back().
template <typename T, std::size_t InlineCapacity = 4>
class ScopedThreadLocalStack {
  static_assert(InlineCapacity != 0, "thread-local stack needs inline storage");

public:
  ScopedThreadLocalStack() noexcept = default;
  ScopedThreadLocalStack(const ScopedThreadLocalStack &) = delete;
  ScopedThreadLocalStack &operator=(const ScopedThreadLocalStack &) = delete;

  bool empty() const noexcept { return size() == 0; }

  std::size_t size() const noexcept {
    return InlineSize + (Overflow ? Overflow->size() : 0);
  }

  T &back() noexcept {
    assert(!empty() && "accessing an empty thread-local stack");
    if (Overflow)
      return Overflow->back();
    return *inlineValue(InlineSize - 1);
  }

  const T &back() const noexcept {
    assert(!empty() && "accessing an empty thread-local stack");
    if (Overflow)
      return Overflow->back();
    return *inlineValue(InlineSize - 1);
  }

  T &push_back(const T &Value) { return emplace_back(Value); }
  T &push_back(T &&Value) { return emplace_back(std::move(Value)); }

  template <typename... Args> T &emplace_back(Args &&...Arguments) {
    if (InlineSize < InlineCapacity) {
      T *Value = ::new (static_cast<void *>(&InlineStorage[InlineSize]))
          T(std::forward<Args>(Arguments)...);
      ++InlineSize;
      return *Value;
    }

    if (Overflow)
      return Overflow->emplace_back(std::forward<Args>(Arguments)...);

    auto NewOverflow = std::make_unique<std::vector<T>>();
    NewOverflow->emplace_back(std::forward<Args>(Arguments)...);
    T &Value = NewOverflow->back();
    Overflow = NewOverflow.release();
    return Value;
  }

  void pop_back() noexcept(std::is_nothrow_destructible_v<T>) {
    assert(!empty() && "removing from an empty thread-local stack");
    if (Overflow) {
      Overflow->pop_back();
      if (Overflow->empty()) {
        std::vector<T> *Released = std::exchange(Overflow, nullptr);
        delete Released;
      }
      return;
    }

    --InlineSize;
    inlineValue(InlineSize)->~T();
  }

  template <typename Predicate> bool any_of(Predicate &&Matches) const {
    for (std::size_t Index = 0; Index != InlineSize; ++Index)
      if (Matches(*inlineValue(Index)))
        return true;
    if (Overflow)
      for (const T &Value : *Overflow)
        if (Matches(Value))
          return true;
    return false;
  }

private:
  using StorageType = std::aligned_storage_t<sizeof(T), alignof(T)>;

  T *inlineValue(std::size_t Index) noexcept {
    return std::launder(reinterpret_cast<T *>(&InlineStorage[Index]));
  }

  const T *inlineValue(std::size_t Index) const noexcept {
    return std::launder(reinterpret_cast<const T *>(&InlineStorage[Index]));
  }

  StorageType InlineStorage[InlineCapacity];
  std::size_t InlineSize = 0;
  std::vector<T> *Overflow = nullptr;
};

} // namespace neverc

#endif // NEVERC_FOUNDATION_CORE_THREADLOCALSTORAGE_H
