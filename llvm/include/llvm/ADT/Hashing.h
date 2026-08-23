//===-- llvm/ADT/Hashing.h - Utilities for hashing --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the newly proposed standard C++ interfaces for hashing
// arbitrary data and building hash functions for user-defined types. This
// interface was originally proposed in N3333[1] and is currently under review
// for inclusion in a future TR and/or standard.
//
// The primary interfaces provide are comprised of one type and three functions:
//
//  -- 'hash_code' class is an opaque type representing the hash code for some
//     data. It is the intended product of hashing, and can be used to implement
//     hash tables, checksumming, and other common uses of hashes. It is not an
//     integer type (although it can be converted to one) because it is risky
//     to assume much about the internals of a hash_code. In particular, each
//     execution of the program has a high probability of producing a different
//     hash_code for a given input. Thus their values are not stable to save or
//     persist, and should only be used during the execution for the
//     construction of hashing datastructures.
//
//  -- 'hash_value' is a function designed to be overloaded for each
//     user-defined type which wishes to be used within a hashing context. It
//     should be overloaded within the user-defined type's namespace and found
//     via ADL. Overloads for primitive types are provided by this library.
//
//  -- 'hash_combine' and 'hash_combine_range' are functions designed to aid
//      programmers in easily and intuitively combining a set of data into
//      a single hash_code for their object. They should only logically be used
//      within the implementation of a 'hash_value' routine or similar context.
//
// 'hash_combine_range' hashes the byte stream of the range via xxh3. The
// contiguous-array overload hashes the range in place; the iterator overload
// materializes the byte stream into a 256-byte on-stack buffer, falling back
// to the heap for ranges that exceed it.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_HASHING_H
#define LLVM_ADT_HASHING_H

#include "csupport/xxhash.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <array>
#include <assert.h>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "llvm/ADT/StringRef.h"

namespace llvm {
template <typename T, typename Enable> struct DenseMapInfo;

/// An opaque object representing a hash code.
///
/// This object represents the result of hashing some entity. It is intended to
/// be used to implement hashtables or other hashing-based data structures.
/// While it wraps and exposes a numeric value, this value should not be
/// trusted to be stable or predictable across processes or executions.
///
/// In order to obtain the hash_code for an object 'x':
/// \code
///   using llvm::hash_value;
///   llvm::hash_code code = hash_value(x);
/// \endcode
class hash_code {
  size_t value;

public:
  /// Default construct a hash_code.
  /// Note that this leaves the value uninitialized.
  hash_code() = default;

  /// Form a hash code directly from a numerical value.
  hash_code(size_t value) : value(value) {}

  /// Convert the hash code to its numerical value for use.
  /*explicit*/ operator size_t() const { return value; }

  friend bool operator==(const hash_code &lhs, const hash_code &rhs) {
    return lhs.value == rhs.value;
  }
  friend bool operator!=(const hash_code &lhs, const hash_code &rhs) {
    return lhs.value != rhs.value;
  }

  /// Allow a hash_code to be directly run through hash_value.
  friend size_t hash_value(const hash_code &code) { return code.value; }
};

/// Compute a hash_code for any integer value.
///
/// Note that this function is intended to compute the same hash_code for
/// a particular value without regard to the pre-promotion type. This is in
/// contrast to hash_combine which may produce different hash_codes for
/// differing argument types even if they would implicit promote to a common
/// type without changing the value.
template <typename T>
std::enable_if_t<is_integral_or_enum<T>::value, hash_code> hash_value(T value);

/// Compute a hash_code for a pointer's address.
///
/// N.B.: This hashes the *address*. Not the value and not the type.
template <typename T> hash_code hash_value(const T *ptr);

/// Compute a hash_code for a pair of objects.
template <typename T, typename U>
hash_code hash_value(const std::pair<T, U> &arg);

/// Compute a hash_code for a tuple.
template <typename... Ts> hash_code hash_value(const std::tuple<Ts...> &arg);

/// Compute a hash_code for a standard string.
template <typename T> hash_code hash_value(const std::basic_string<T> &arg);

/// Compute a hash_code for a standard string.
template <typename T> hash_code hash_value(const std::optional<T> &arg);

/// Override the execution seed with a fixed value.
///
/// This hashing library uses a per-execution seed designed to change on each
/// run with high probability in order to ensure that the hash codes are not
/// attackable and to ensure that output which is intended to be stable does
/// not rely on the particulars of the hash codes produced.
///
/// That said, there are use cases where it is important to be able to
/// reproduce *exactly* a specific behavior. To that end, we provide a function
/// which will forcibly set the seed to a fixed value. This must be done at the
/// start of the program, before any hashes are computed. Also, it cannot be
/// undone. This makes it thread-hostile and very hard to use outside of
/// immediately on start of a simple program designed for reproducible
/// behavior.
void set_fixed_execution_hash_seed(uint64_t fixed_value);

// All of the implementation details of actually computing the various hash
// code values are held within this namespace. These routines are included in
// the header file mainly to allow inlining and constant propagation.
namespace hashing {
namespace detail {

/// A global, fixed seed-override variable.
///
/// This variable can be set using the \see llvm::set_fixed_execution_hash_seed
/// function. See that function for details. Do not, under any circumstances,
/// set or read this variable.
inline uint64_t fixed_seed_override = 0;

inline uint64_t get_execution_seed() {
  const uint64_t seed_prime = 0xff51afd7ed558ccdULL;
  static uint64_t seed = fixed_seed_override ? fixed_seed_override : seed_prime;
  return seed;
}

/// Hash a contiguous byte buffer to a hash_code. The execution seed is XORed
/// into the result (not propagated through the avalanche).
inline hash_code combine_bytes(const char *data, size_t len) {
  return csupport_xxh3_64bits(reinterpret_cast<const uint8_t *>(data), len) ^
         get_execution_seed();
}

/// Murmur-inspired 16-byte mixer. Kept for StructuralHash and similar
/// incremental folding that wants a fully-inline combine of two words.
inline uint64_t hash_16_bytes(uint64_t low, uint64_t high) {
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (low ^ high) * kMul;
  a ^= (a >> 47);
  uint64_t b = (high ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

/// Trait to indicate whether a type's bits can be hashed directly.
///
/// A type trait which is true if we want to combine values for hashing by
/// reading the underlying data. It is false if values of this type must
/// first be passed to hash_value, and the resulting hash_codes combined.
template <typename T>
struct is_hashable_data
    : std::integral_constant<bool, ((is_integral_or_enum<T>::value ||
                                     std::is_pointer<T>::value) &&
                                    64 % sizeof(T) == 0)> {};

// Special case std::pair to detect when both types are viable and when there
// is no alignment-derived padding in the pair. This is a bit of a lie because
// std::pair isn't truly POD, but it's close enough in all reasonable
// implementations for our use case of hashing the underlying data.
template <typename T, typename U>
struct is_hashable_data<std::pair<T, U>>
    : std::integral_constant<
          bool, (is_hashable_data<T>::value && is_hashable_data<U>::value &&
                 (sizeof(T) + sizeof(U)) == sizeof(std::pair<T, U>))> {};

/// Helper to get the hashable data representation for a type.
/// This variant is enabled when the type itself can be used.
template <typename T>
std::enable_if_t<is_hashable_data<T>::value, T>
get_hashable_data(const T &value) {
  return value;
}
/// Helper to get the hashable data representation for a type.
/// This variant is enabled when we must first call hash_value and use the
/// result as our data.
template <typename T>
std::enable_if_t<!is_hashable_data<T>::value, size_t>
get_hashable_data(const T &value) {
  using ::llvm::hash_value;
  return hash_value(value);
}

/// Implement the combining of values into a hash_code.
///
/// xxh3 has no streaming entry point in CSupport, so the byte stream is
/// flattened to a buffer and hashed in one shot. The 256-byte on-stack buffer
/// holds 32 pointer-sized values, which covers virtually all in-tree
/// non-contiguous callers.
template <typename InputIteratorT>
hash_code hash_combine_range_impl(InputIteratorT first, InputIteratorT last) {
  alignas(uint64_t) char stack_buf[256];
  std::unique_ptr<char[]> heap_buf;
  char *buf = stack_buf;
  size_t cap = sizeof(stack_buf);
  size_t len = 0;
  for (; first != last; ++first) {
    auto data = get_hashable_data(*first);
    if (len + sizeof(data) > cap) {
      size_t new_cap = cap * 2;
      while (new_cap < len + sizeof(data))
        new_cap *= 2;
      std::unique_ptr<char[]> new_buf(new char[new_cap]);
      std::memcpy(new_buf.get(), buf, len);
      heap_buf = std::move(new_buf);
      buf = heap_buf.get();
      cap = new_cap;
    }
    std::memcpy(buf + len, &data, sizeof(data));
    len += sizeof(data);
  }
  return combine_bytes(buf, len);
}

/// Contiguous pointer range: hash the underlying bytes in place.
template <typename ValueT>
std::enable_if_t<is_hashable_data<ValueT>::value, hash_code>
hash_combine_range_impl(ValueT *first, ValueT *last) {
  return combine_bytes(reinterpret_cast<const char *>(first),
                       size_t(last - first) * sizeof(ValueT));
}

/// Sum of `sizeof(get_hashable_data(arg))` across a parameter pack.
template <typename... Ts> constexpr size_t total_hashable_size() {
  return (size_t(0) + ... +
          sizeof(decltype(get_hashable_data(std::declval<const Ts &>()))));
}

/// Copy `get_hashable_data(arg)` into `buf` at offset `off`, advancing `off`.
template <typename T>
inline void store_hashable_data(char *buf, size_t &off, const T &arg) {
  auto data = get_hashable_data(arg);
  std::memcpy(buf + off, &data, sizeof(data));
  off += sizeof(data);
}

} // namespace detail
} // namespace hashing

/// Compute a hash_code for a sequence of values.
///
/// This hashes a sequence of values. It produces the same hash_code as
/// 'hash_combine(a, b, c, ...)', but can run over arbitrary sized sequences
/// and is significantly faster given pointers and types which can be hashed as
/// a sequence of bytes.
template <typename InputIteratorT>
hash_code hash_combine_range(InputIteratorT first, InputIteratorT last) {
  return ::llvm::hashing::detail::hash_combine_range_impl(first, last);
}

/// Combine values into a single hash_code.
///
/// This routine accepts a varying number of arguments of any type. It will
/// attempt to combine them into a single hash_code. For user-defined types it
/// attempts to call a \see hash_value overload (via ADL) for the type. For
/// integer and pointer types it directly combines their data into the
/// resulting hash_code.
///
/// The result is suitable for returning from a user's hash_value
/// *implementation* for their user-defined type. Consumers of a type should
/// *not* call this routine, they should instead call 'hash_value'.
template <typename... Ts> hash_code hash_combine(const Ts &...args) {
  constexpr size_t Total = hashing::detail::total_hashable_size<Ts...>();
  // Round up so `data()` is non-null when Total == 0; combine_bytes won't
  // read the buffer in that case (len=0 short-circuits in xxh3).
  std::array<char, (Total < 1 ? 1 : Total)> buf{};
  size_t off = 0;
  if constexpr (sizeof...(Ts) != 0)
    (hashing::detail::store_hashable_data(buf.data(), off, args), ...);
  return hashing::detail::combine_bytes(buf.data(), Total);
}

// Implementation details for implementations of hash_value overloads provided
// here.
namespace hashing {
namespace detail {

/// Helper to hash the value of a single integer.
///
/// Overloads for smaller integer types are not provided to ensure consistent
/// behavior in the presence of integral promotions. Essentially,
/// "hash_value('4')" and "hash_value('0' + 4)" should be the same.
inline hash_code hash_integer_value(uint64_t value) {
  // splitmix64 finalizer (xmxmx).
  uint64_t x = value ^ get_execution_seed();
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

} // namespace detail
} // namespace hashing

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T>
std::enable_if_t<is_integral_or_enum<T>::value, hash_code> hash_value(T value) {
  return ::llvm::hashing::detail::hash_integer_value(
      static_cast<uint64_t>(value));
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T> hash_code hash_value(const T *ptr) {
  return ::llvm::hashing::detail::hash_integer_value(
      reinterpret_cast<uintptr_t>(ptr));
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T, typename U>
hash_code hash_value(const std::pair<T, U> &arg) {
  return hash_combine(arg.first, arg.second);
}

template <typename... Ts> hash_code hash_value(const std::tuple<Ts...> &arg) {
  return std::apply([](const auto &...xs) { return hash_combine(xs...); }, arg);
}

// Declared and documented above, but defined here so that any of the hashing
// infrastructure is available.
template <typename T> hash_code hash_value(const std::basic_string<T> &arg) {
  return hash_combine_range(arg.begin(), arg.end());
}

template <typename T> hash_code hash_value(const std::optional<T> &arg) {
  return arg ? hash_combine(true, *arg) : hash_value(false);
}

template <> struct DenseMapInfo<hash_code, void> {
  static inline hash_code getEmptyKey() { return hash_code(-1); }
  static inline hash_code getTombstoneKey() { return hash_code(-2); }
  static unsigned getHashValue(hash_code val) { return val; }
  static bool isEqual(hash_code LHS, hash_code RHS) { return LHS == RHS; }
};

inline hash_code hash_value(StringRef S) {
  return hashing::detail::combine_bytes(S.data(), S.size());
}

inline unsigned DenseMapInfo<StringRef, void>::getHashValue(StringRef Val) {
  assert(Val.data() != getEmptyKey().data() && "Cannot hash the empty key!");
  assert(Val.data() != getTombstoneKey().data() &&
         "Cannot hash the tombstone key!");
  return (unsigned)(hash_value(Val));
}

inline void set_fixed_execution_hash_seed(uint64_t v) {
  hashing::detail::fixed_seed_override = v;
}

} // namespace llvm

/// Implement std::hash so that hash_code can be used in STL containers.
namespace std {

template <> struct hash<llvm::hash_code> {
  size_t operator()(llvm::hash_code const &Val) const { return Val; }
};

} // namespace std

#endif
