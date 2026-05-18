//===--- JSON.h - JSON values, parsing and serialization -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
///
/// \file
/// This file supports working with JSON data.
///
/// It comprises:
///
/// - classes which hold dynamically-typed parsed JSON structures
///   These are value types that can be composed, inspected, and modified.
///   See json::Value, and the related types json::Object and json::Array.
///
/// - functions to parse JSON text into Values, and to serialize Values to text.
///   See parse(), operator<<, and format_provider.
///
/// - a convention and helpers for mapping between json::Value and user-defined
///   types. See fromJSON(), ObjectMapper, and the class comment on Value.
///
/// - an output API json::OStream which can emit JSON without materializing
///   all structures as json::Value.
///
/// Typically, JSON data would be read from an external source, parsed into
/// a Value, and then converted into some native data structure before doing
/// real work on it. (And vice versa when writing).
///
/// Other serialization mechanisms you may consider:
///
/// - YAML is also text-based, and more human-readable than JSON. It's a more
///   complex format and data model, and YAML parsers aren't ubiquitous.
///   YAMLParser.h is a streaming parser suitable for parsing large documents
///   (including JSON, as YAML is a superset). It can be awkward to use
///   directly. YAML I/O (YAMLTraits.h) provides data mapping that is more
///   declarative than the toJSON/fromJSON conventions here.
///
/// - LLVM bitstream is a space- and CPU- efficient binary format. Typically it
///   encodes LLVM IR ("bitcode"), but it can be a container for other data.
///   Low-level reader/writer libraries are in Bitstream/Bitstream*.h
///
//===---------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_JSON_H
#define LLVM_SUPPORT_JSON_H

#include "csupport/cpp_compat_stl.h"
#include "csupport/lconvert_lu_lt_lf_lwrapper.h"
#include "csupport/lj_ls_lo_ln.h"
#include "csupport/lsource_lmgr.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <math.h>

namespace llvm {
namespace json {

// === String encodings ===
//
// JSON strings are character sequences (not byte sequences like std::string).
// We need to know the encoding, and for simplicity only support UTF-8.
//
//   - When parsing, invalid UTF-8 is a syntax error like any other
//
//   - When creating Values from strings, callers must ensure they are UTF-8.
//        with asserts on, invalid UTF-8 will crash the program
//        with asserts off, we'll substitute the replacement character (U+FFFD)
//     Callers can use json::isUTF8() and json::fixUTF8() for validation.
//
//   - When retrieving strings from Values (e.g. asString()), the result will
//     always be valid UTF-8.

template <typename T>
constexpr bool is_uint_64_bit_v =
    std::is_integral_v<T> && std::is_unsigned_v<T> &&
    sizeof(T) == sizeof(uint64_t);

/// Returns true if \p S is valid UTF-8, which is required for use as JSON.
/// If it returns false, \p Offset is set to a byte offset near the first error.
bool isUTF8(llvm::StringRef S, size_t *ErrOffset = nullptr);
/// Replaces invalid UTF-8 sequences in \p S with the replacement character
/// (U+FFFD). The returned string is valid UTF-8.
/// This is much slower than isUTF8, so test that first.
SmallString<256> fixUTF8(llvm::StringRef S);

class Array;
class ObjectKey;
class Value;
template <typename T> Value toJSON(const std::optional<T> &Opt);

/// An Object is a JSON object, which maps strings to heterogenous JSON values.
/// It simulates DenseMap<ObjectKey, Value>. ObjectKey is a maybe-owned string.
class Object {
  using Storage = DenseMap<ObjectKey, Value, llvm::DenseMapInfo<StringRef>>;
  Storage M;

public:
  using key_type = ObjectKey;
  using mapped_type = Value;
  using value_type = Storage::value_type;
  using iterator = Storage::iterator;
  using const_iterator = Storage::const_iterator;

  Object() = default;
  // KV is a trivial key-value struct for list-initialization.
  // (using std::pair forces extra copies).
  struct KV;
  explicit Object(std::initializer_list<KV> Properties);

  iterator begin() { return M.begin(); }
  const_iterator begin() const { return M.begin(); }
  iterator end() { return M.end(); }
  const_iterator end() const { return M.end(); }

  bool empty() const { return M.empty(); }
  size_t size() const { return M.size(); }

  void clear() { M.clear(); }
  std::pair<iterator, bool> insert(KV E);
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const ObjectKey &K, Ts &&...Args) {
    return M.try_emplace(K, std::forward<Ts>(Args)...);
  }
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(ObjectKey &&K, Ts &&...Args) {
    return M.try_emplace(std::move(K), std::forward<Ts>(Args)...);
  }
  bool erase(StringRef K);
  void erase(iterator I) { M.erase(I); }

  iterator find(StringRef K) { return M.find_as(K); }
  const_iterator find(StringRef K) const { return M.find_as(K); }
  // operator[] acts as if Value was default-constructible as null.
  Value &operator[](const ObjectKey &K);
  Value &operator[](ObjectKey &&K);
  // Look up a property, returning nullptr if it doesn't exist.
  Value *get(StringRef K);
  const Value *get(StringRef K) const;
  // Typed accessors: getNull/getBoolean return tribool/bool sentinel.
  // getNumber returns NaN if absent/wrong type.
  // getInteger/getString use out-param or null-data sentinel.
  bool getNull(StringRef K) const;
  int getBoolean(StringRef K) const;
  double getNumber(StringRef K) const;
  bool getInteger(StringRef K, int64_t &Out) const;
  llvm::StringRef getString(StringRef K) const;
  const json::Object *getObject(StringRef K) const;
  json::Object *getObject(StringRef K);
  const json::Array *getArray(StringRef K) const;
  json::Array *getArray(StringRef K);
};
bool operator==(const Object &LHS, const Object &RHS);
inline bool operator!=(const Object &LHS, const Object &RHS) {
  return !(LHS == RHS);
}

/// An Array is a JSON array, which contains heterogeneous JSON values.
/// It simulates std::vector<Value>.
class Array {
  std::vector<Value> V;

public:
  using value_type = Value;
  using iterator = std::vector<Value>::iterator;
  using const_iterator = std::vector<Value>::const_iterator;

  Array() = default;
  explicit Array(std::initializer_list<Value> Elements);
  template <typename Collection> explicit Array(const Collection &C) {
    for (const auto &V : C)
      emplace_back(V);
  }

  Value &operator[](size_t I);
  const Value &operator[](size_t I) const;
  Value &front();
  const Value &front() const;
  Value &back();
  const Value &back() const;
  Value *data();
  const Value *data() const;

  iterator begin();
  const_iterator begin() const;
  iterator end();
  const_iterator end() const;

  bool empty() const;
  size_t size() const;
  void reserve(size_t S);

  void clear();
  void push_back(const Value &E);
  void push_back(Value &&E);
  template <typename... Args> void emplace_back(Args &&...A);
  void pop_back();
  iterator insert(const_iterator P, const Value &E);
  iterator insert(const_iterator P, Value &&E);
  template <typename It> iterator insert(const_iterator P, It A, It Z);
  template <typename... Args> iterator emplace(const_iterator P, Args &&...A);

  friend bool operator==(const Array &L, const Array &R);
};
inline bool operator!=(const Array &L, const Array &R) { return !(L == R); }

/// A Value is an JSON value of unknown type.
/// They can be copied, but should generally be moved.
///
/// === Composing values ===
///
/// You can implicitly construct Values from:
///   - strings: std::string, SmallString, formatv, StringRef, char*
///              (char*, and StringRef are references, not copies!)
///   - numbers
///   - booleans
///   - null: nullptr
///   - arrays: {"foo", 42.0, false}
///   - serializable things: types with toJSON(const T&)->Value, found by ADL
///
/// They can also be constructed from object/array helpers:
///   - json::Object is a type like map<ObjectKey, Value>
///   - json::Array is a type like vector<Value>
/// These can be list-initialized, or used to build up collections in a loop.
/// json::ary(Collection) converts all items in a collection to Values.
///
/// === Inspecting values ===
///
/// Each Value is one of the JSON kinds:
///   null    (nullptr_t)
///   boolean (bool)
///   number  (double, int64 or uint64)
///   string  (StringRef)
///   array   (json::Array)
///   object  (json::Object)
///
/// The kind can be queried directly, or implicitly via the typed accessors:
///   StringRef S = E.getAsString(); if (S.data()
///     assert(E.kind() == Value::String);
///
/// Array and Object also have typed indexing accessors for easy traversal:
///   Expected<Value> E = parse(R"( {"options": {"font": "sans-serif"}} )");
///   if (Object* O = E->getAsObject())
///     if (Object* Opts = O->getObject("options"))
///       StringRef Font = Opts->getString("font"); if (Font.data())
///         assert(Opts->at("font").kind() == Value::String);
///
/// === Converting JSON values to C++ types ===
///
/// The convention is to have a deserializer function findable via ADL:
///     fromJSON(const json::Value&, T&, Path) -> bool
///
/// The return value indicates overall success, and Path is used for precise
/// error reporting. (The Path::Root passed in at the top level fromJSON call
/// captures any nested error and can render it in context).
/// If conversion fails, fromJSON calls Path::report() and immediately returns.
/// This ensures that the first fatal error survives.
///
/// Deserializers are provided for:
///   - bool
///   - int and int64_t
///   - double
///   - std::string
///   - vector<T>, where T is deserializable
///   - map<string, T>, where T is deserializable
///   - std::optional<T>, where T is deserializable
/// ObjectMapper can help writing fromJSON() functions for object types.
///
/// For conversion in the other direction, the serializer function is:
///    toJSON(const T&) -> json::Value
/// If this exists, then it also allows constructing Value from T, and can
/// be used to serialize vector<T>, map<string, T>, and std::optional<T>.
///
/// === Serialization ===
///
/// Values can be serialized to JSON:
///   1) raw_ostream << Value                    // Basic formatting.
///   2) raw_ostream << formatv("{0}", Value)    // Basic formatting.
///   3) raw_ostream << formatv("{0:2}", Value)  // Pretty-print with indent 2.
///
/// And parsed:
///   Expected<Value> E = json::parse("[1, 2, null]");
///   assert(E && E->kind() == Value::Array);
class Value {
public:
  enum Kind {
    Null,
    Boolean,
    /// Number values can store both int64s and doubles at full precision,
    /// depending on what they were constructed/parsed from.
    Number,
    String,
    Array,
    Object,
  };

  // It would be nice to have Value() be null. But that would make {} null too.
  Value(const Value &M) { copyFrom(M); }
  Value(Value &&M) { moveFrom(std::move(M)); }
  Value(std::initializer_list<Value> Elements);
  Value(json::Array &&Elements) : Type(T_Array) {
    create<json::Array>(std::move(Elements));
  }
  template <typename Elt>
  Value(const std::vector<Elt> &C) : Value(json::Array(C)) {}
  Value(json::Object &&Properties) : Type(T_Object) {
    create<json::Object>(std::move(Properties));
  }
  template <typename Elt, typename KeyT>
  Value(const std::map<KeyT, Elt> &C) : Value(json::Object(C)) {}
  // Strings: types with value semantics. Must be valid UTF-8.
  // Internal storage uses SmallString<0> instead of std::string.
  Value(const llvm::SmallVectorImpl<char> &V) : Type(T_String) {
    StringRef S(V.data(), V.size());
    if (LLVM_UNLIKELY(!isUTF8(S))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      auto Fixed = fixUTF8(S);
      create<SmallString<0>>(
          SmallString<0>(Fixed.data(), Fixed.data() + Fixed.size()));
    } else {
      create<SmallString<0>>(SmallString<0>(V.begin(), V.end()));
    }
  }
  Value(const std::basic_string<char> &V) : Value(StringRef(V)) {}
  Value(const llvm::formatv_object_base &V) : Value(V.str()) {}
  // Strings: types with reference semantics. Must be valid UTF-8.
  Value(StringRef V) : Type(T_StringRef) {
    create<llvm::StringRef>(V);
    if (LLVM_UNLIKELY(!isUTF8(V))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = Value(fixUTF8(V));
    }
  }
  Value(const char *V) : Value(StringRef(V)) {}
  Value(std::nullptr_t) : Type(T_Null) {}
  // Boolean (disallow implicit conversions).
  // (The last template parameter is a dummy to keep templates distinct.)
  template <typename T, typename = std::enable_if_t<std::is_same_v<T, bool>>,
            bool = false>
  Value(T B) : Type(T_Boolean) {
    create<bool>(B);
  }

  // Unsigned 64-bit integers.
  template <typename T, typename = std::enable_if_t<is_uint_64_bit_v<T>>>
  Value(T V) : Type(T_UINT64) {
    create<uint64_t>(uint64_t{V});
  }

  // Integers (except boolean and uint64_t).
  // Must be non-narrowing convertible to int64_t.
  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>,
            typename = std::enable_if_t<!std::is_same_v<T, bool>>,
            typename = std::enable_if_t<!is_uint_64_bit_v<T>>>
  Value(T I) : Type(T_Integer) {
    create<int64_t>(int64_t{I});
  }
  // Floating point. Must be non-narrowing convertible to double.
  template <typename T,
            typename = std::enable_if_t<std::is_floating_point_v<T>>,
            double * = nullptr>
  Value(T D) : Type(T_Double) {
    create<double>(double{D});
  }
  // Serializable types: with a toJSON(const T&)->Value function, found by ADL.
  template <typename T,
            typename = std::enable_if_t<
                std::is_same_v<Value, decltype(toJSON(*(const T *)nullptr))>>,
            Value * = nullptr>
  Value(const T &V) : Value(toJSON(V)) {}

  Value &operator=(const Value &M) {
    destroy();
    copyFrom(M);
    return *this;
  }
  Value &operator=(Value &&M) {
    destroy();
    moveFrom(std::move(M));
    return *this;
  }
  ~Value() { destroy(); }

  Kind kind() const {
    switch (Type) {
    case T_Null:
      return Null;
    case T_Boolean:
      return Boolean;
    case T_Double:
    case T_Integer:
    case T_UINT64:
      return Number;
    case T_String:
    case T_StringRef:
      return String;
    case T_Object:
      return Object;
    case T_Array:
      return Array;
    }
    llvm_unreachable("Unknown kind");
  }

  // Typed accessors: isNull/getAsBoolean use bool/tribool sentinels.
  // getAsNumber returns NaN if not a number (JSON spec forbids NaN literals).
  // getAsInteger/getAsUINT64 use output parameters.
  // getAsString returns StringRef() (data()==nullptr) if not a string.
  bool isNull() const { return Type == T_Null; }
  int getAsBoolean() const {
    if (LLVM_LIKELY(Type == T_Boolean))
      return as<bool>() ? 1 : 0;
    return -1;
  }
  double getAsNumber() const {
    if (LLVM_LIKELY(Type == T_Double))
      return as<double>();
    if (LLVM_LIKELY(Type == T_Integer))
      return (double)as<int64_t>();
    if (LLVM_LIKELY(Type == T_UINT64))
      return (double)as<uint64_t>();
    return __builtin_nan("");
  }
  bool getAsInteger(int64_t &Out) const {
    if (LLVM_LIKELY(Type == T_Integer)) {
      Out = as<int64_t>();
      return true;
    }
    if (LLVM_LIKELY(Type == T_UINT64)) {
      uint64_t U = as<uint64_t>();
      if (LLVM_LIKELY(U <= (uint64_t)INT64_MAX)) {
        Out = (int64_t)U;
        return true;
      }
    }
    if (LLVM_LIKELY(Type == T_Double)) {
      double D = as<double>();
      if (LLVM_LIKELY(std::modf(D, &D) == 0.0 && D >= (double)INT64_MIN &&
                      D <= (double)INT64_MAX)) {
        Out = (int64_t)D;
        return true;
      }
    }
    return false;
  }
  bool getAsUINT64(uint64_t &Out) const {
    if (Type == T_UINT64) {
      Out = as<uint64_t>();
      return true;
    }
    if (Type == T_Integer) {
      int64_t N = as<int64_t>();
      if (N >= 0) {
        Out = (uint64_t)N;
        return true;
      }
    }
    return false;
  }
  llvm::StringRef getAsString() const {
    if (Type == T_String) {
      auto &S = as<SmallString<0>>();
      return llvm::StringRef(S.data(), S.size());
    }
    if (LLVM_LIKELY(Type == T_StringRef))
      return as<llvm::StringRef>();
    return llvm::StringRef();
  }
  const json::Object *getAsObject() const {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  json::Object *getAsObject() {
    return LLVM_LIKELY(Type == T_Object) ? &as<json::Object>() : nullptr;
  }
  const json::Array *getAsArray() const {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }
  json::Array *getAsArray() {
    return LLVM_LIKELY(Type == T_Array) ? &as<json::Array>() : nullptr;
  }

private:
  void destroy();
  void copyFrom(const Value &M);
  // We allow moving from *const* Values, by marking all members as mutable!
  // This hack is needed to support initializer-list syntax efficiently.
  // (std::initializer_list<T> is a container of const T).
  void moveFrom(const Value &&M);
  friend class Array;
  friend class Object;

  template <typename T, typename... U> void create(U &&...V) {
    new (reinterpret_cast<T *>(&Union)) T(std::forward<U>(V)...);
  }
  template <typename T> T &as() const {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC6 and earlier.
    void *Storage = static_cast<void *>(&Union);
    return *static_cast<T *>(Storage);
  }

  friend class OStream;

  enum ValueType : char16_t {
    T_Null,
    T_Boolean,
    T_Double,
    T_Integer,
    T_UINT64,
    T_StringRef,
    T_String,
    T_Object,
    T_Array,
  };
  // All members mutable, see moveFrom().
  mutable ValueType Type;
  mutable llvm::AlignedCharArrayUnion<bool, double, int64_t, uint64_t,
                                      llvm::StringRef, SmallString<0>,
                                      json::Array, json::Object>
      Union;
  friend bool operator==(const Value &, const Value &);
};

bool operator==(const Value &, const Value &);
inline bool operator!=(const Value &L, const Value &R) { return !(L == R); }

// Array Methods
inline Value &Array::operator[](size_t I) { return V[I]; }
inline const Value &Array::operator[](size_t I) const { return V[I]; }
inline Value &Array::front() { return V.front(); }
inline const Value &Array::front() const { return V.front(); }
inline Value &Array::back() { return V.back(); }
inline const Value &Array::back() const { return V.back(); }
inline Value *Array::data() { return V.data(); }
inline const Value *Array::data() const { return V.data(); }

inline typename Array::iterator Array::begin() { return V.begin(); }
inline typename Array::const_iterator Array::begin() const { return V.begin(); }
inline typename Array::iterator Array::end() { return V.end(); }
inline typename Array::const_iterator Array::end() const { return V.end(); }

inline bool Array::empty() const { return V.empty(); }
inline size_t Array::size() const { return V.size(); }
inline void Array::reserve(size_t S) { V.reserve(S); }

inline void Array::clear() { V.clear(); }
inline void Array::push_back(const Value &E) { V.push_back(E); }
inline void Array::push_back(Value &&E) { V.push_back(std::move(E)); }
template <typename... Args> inline void Array::emplace_back(Args &&...A) {
  V.emplace_back(std::forward<Args>(A)...);
}
inline void Array::pop_back() { V.pop_back(); }
inline typename Array::iterator Array::insert(const_iterator P,
                                              const Value &E) {
  return V.insert(P, E);
}
inline typename Array::iterator Array::insert(const_iterator P, Value &&E) {
  return V.insert(P, std::move(E));
}
template <typename It>
inline typename Array::iterator Array::insert(const_iterator P, It A, It Z) {
  return V.insert(P, A, Z);
}
template <typename... Args>
inline typename Array::iterator Array::emplace(const_iterator P, Args &&...A) {
  return V.emplace(P, std::forward<Args>(A)...);
}
inline bool operator==(const Array &L, const Array &R) { return L.V == R.V; }

/// ObjectKey is a used to capture keys in Object. Like Value but:
///   - only strings are allowed
///   - it's optimized for the string literal case (Owned == nullptr)
/// Like Value, strings must be UTF-8. See isUTF8 documentation for details.
class ObjectKey {
public:
  ObjectKey(const char *S) : ObjectKey(StringRef(S)) {}
  ObjectKey(const std::basic_string<char> &S) : ObjectKey(StringRef(S)) {}
  ObjectKey(llvm::StringRef S) : OwnedBuf(0), Data(S) {
    if (LLVM_UNLIKELY(!isUTF8(Data))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = ObjectKey(fixUTF8(S));
    }
  }
  ObjectKey(const llvm::SmallVectorImpl<char> &V) : OwnedBuf(0) {
    OwnedBuf = (char *)malloc(V.size());
    memcpy(OwnedBuf, V.data(), V.size());
    Data = llvm::StringRef(OwnedBuf, V.size());
    if (LLVM_UNLIKELY(!isUTF8(Data))) {
      assert(false && "Invalid UTF-8 in value used as JSON");
      *this = ObjectKey(fixUTF8(Data));
    }
  }
  ObjectKey(const llvm::formatv_object_base &V) : ObjectKey(V.str()) {}

  ObjectKey(const ObjectKey &C) : OwnedBuf(0) { *this = C; }
  ObjectKey(ObjectKey &&C) : OwnedBuf(C.OwnedBuf), Data(C.Data) {
    C.OwnedBuf = 0;
    C.Data = llvm::StringRef();
  }
  ObjectKey &operator=(const ObjectKey &C) {
    free(OwnedBuf);
    if (C.OwnedBuf) {
      OwnedBuf = (char *)malloc(C.Data.size());
      memcpy(OwnedBuf, C.Data.data(), C.Data.size());
      Data = llvm::StringRef(OwnedBuf, C.Data.size());
    } else {
      OwnedBuf = 0;
      Data = C.Data;
    }
    return *this;
  }
  ObjectKey &operator=(ObjectKey &&C) {
    free(OwnedBuf);
    OwnedBuf = C.OwnedBuf;
    Data = C.Data;
    C.OwnedBuf = 0;
    C.Data = llvm::StringRef();
    return *this;
  }
  ~ObjectKey() { free(OwnedBuf); }

  operator llvm::StringRef() const { return Data; }
  SmallString<64> str() const { return SmallString<64>(Data); }

private:
  char *OwnedBuf;
  llvm::StringRef Data;
};

inline bool operator==(const ObjectKey &L, const ObjectKey &R) {
  return llvm::StringRef(L) == llvm::StringRef(R);
}
inline bool operator!=(const ObjectKey &L, const ObjectKey &R) {
  return !(L == R);
}
inline bool operator<(const ObjectKey &L, const ObjectKey &R) {
  return StringRef(L) < StringRef(R);
}

struct Object::KV {
  ObjectKey K;
  Value V;
};

inline Object::Object(std::initializer_list<KV> Properties) {
  for (const auto &P : Properties) {
    auto R = try_emplace(P.K, nullptr);
    if (R.second)
      R.first->getSecond().moveFrom(std::move(P.V));
  }
}
inline std::pair<Object::iterator, bool> Object::insert(KV E) {
  return try_emplace(std::move(E.K), std::move(E.V));
}
inline bool Object::erase(StringRef K) { return M.erase(ObjectKey(K)); }

/// A "cursor" marking a position within a Value.
/// The Value is a tree, and this is the path from the root to the current node.
/// This is used to associate errors with particular subobjects.
class Path {
public:
  class Root;

  /// Records that the value at the current path is invalid.
  /// Message is e.g. "expected number" and becomes part of the final error.
  /// This overwrites any previously written error message in the root.
  void report(llvm::StringLiteral Message);

  /// The root may be treated as a Path.
  Path(Root &R) : Parent(nullptr), Seg(&R) {}
  /// Derives a path for an array element: this[Index]
  Path index(unsigned Index) const { return Path(this, Segment(Index)); }
  /// Derives a path for an object field: this.Field
  Path field(StringRef Field) const { return Path(this, Segment(Field)); }

private:
  /// One element in a JSON path: an object field (.foo) or array index [27].
  /// Exception: the root Path encodes a pointer to the Path::Root.
  class Segment {
    uintptr_t Pointer;
    unsigned Offset;

  public:
    Segment() = default;
    Segment(Root *R) : Pointer(reinterpret_cast<uintptr_t>(R)) {}
    Segment(llvm::StringRef Field)
        : Pointer(reinterpret_cast<uintptr_t>(Field.data())),
          Offset(static_cast<unsigned>(Field.size())) {}
    Segment(unsigned Index) : Pointer(0), Offset(Index) {}

    bool isField() const { return Pointer != 0; }
    StringRef field() const {
      return StringRef(reinterpret_cast<const char *>(Pointer), Offset);
    }
    unsigned index() const { return Offset; }
    Root *root() const { return reinterpret_cast<Root *>(Pointer); }
  };

  const Path *Parent;
  Segment Seg;

  Path(const Path *Parent, Segment S) : Parent(Parent), Seg(S) {}
};

/// The root is the trivial Path to the root value.
/// It also stores the latest reported error and the path where it occurred.
class Path::Root {
  llvm::StringRef Name;
  llvm::StringLiteral ErrorMessage;
  std::vector<Path::Segment> ErrorPath; // Only valid in error state. Reversed.

  friend void Path::report(llvm::StringLiteral Message);

public:
  Root(llvm::StringRef Name = "") : Name(Name), ErrorMessage("") {}
  // No copy/move allowed as there are incoming pointers.
  Root(Root &&) = delete;
  Root &operator=(Root &&) = delete;
  Root(const Root &) = delete;
  Root &operator=(const Root &) = delete;

  /// Returns the last error reported, or else a generic error.
  Error getError() const;
  /// Print the root value with the error shown inline as a comment.
  /// Unrelated parts of the value are elided for brevity, e.g.
  ///   {
  ///      "id": 42,
  ///      "name": /* expected string */ null,
  ///      "properties": { ... }
  ///   }
  void printErrorContext(const Value &, llvm::raw_ostream &) const;
};

// Standard deserializers are provided for primitive types.
// See comments on Value.
inline bool fromJSON(const Value &E, SmallVectorImpl<char> &Out, Path P) {
  auto S = E.getAsString();
  if (S.data()) {
    Out.assign(S.begin(), S.end());
    return true;
  }
  P.report("expected string");
  return false;
}
inline bool fromJSON(const Value &E, std::basic_string<char> &Out, Path P) {
  auto S = E.getAsString();
  if (S.data()) {
    Out.assign(S.data(), S.size());
    return true;
  }
  P.report("expected string");
  return false;
}
inline bool fromJSON(const Value &E, int &Out, Path P) {
  int64_t S;
  if (E.getAsInteger(S)) {
    Out = (int)S;
    return true;
  }
  P.report("expected integer");
  return false;
}
inline bool fromJSON(const Value &E, int64_t &Out, Path P) {
  if (E.getAsInteger(Out))
    return true;
  P.report("expected integer");
  return false;
}
inline bool fromJSON(const Value &E, double &Out, Path P) {
  double S = E.getAsNumber();
  if (!__builtin_isnan(S)) {
    Out = S;
    return true;
  }
  P.report("expected number");
  return false;
}
inline bool fromJSON(const Value &E, bool &Out, Path P) {
  int S = E.getAsBoolean();
  if (S >= 0) {
    Out = (bool)S;
    return true;
  }
  P.report("expected boolean");
  return false;
}
inline bool fromJSON(const Value &E, uint64_t &Out, Path P) {
  if (E.getAsUINT64(Out))
    return true;
  P.report("expected uint64_t");
  return false;
}
inline bool fromJSON(const Value &E, std::nullptr_t &Out, Path P) {
  if (E.isNull()) {
    Out = nullptr;
    return true;
  }
  P.report("expected null");
  return false;
}
template <typename T>
bool fromJSON(const Value &E, std::optional<T> &Out, Path P) {
  if (E.isNull()) {
    Out = std::nullopt;
    return true;
  }
  T Result = {};
  if (!fromJSON(E, Result, P))
    return false;
  Out = std::move(Result);
  return true;
}
template <typename T>
bool fromJSON(const Value &E, std::vector<T> &Out, Path P) {
  if (auto *A = E.getAsArray()) {
    Out.clear();
    Out.resize(A->size());
    for (size_t I = 0; I < A->size(); ++I)
      if (!fromJSON((*A)[I], Out[I], P.index(I)))
        return false;
    return true;
  }
  P.report("expected array");
  return false;
}
template <typename T, typename KeyT>
bool fromJSON(const Value &E, std::map<KeyT, T> &Out, Path P) {
  if (auto *O = E.getAsObject()) {
    Out.clear();
    for (const auto &KV : *O)
      if (!fromJSON(KV.second,
                    Out[KeyT(llvm::StringRef(KV.first).begin(),
                             llvm::StringRef(KV.first).end())],
                    P.field(KV.first)))
        return false;
    return true;
  }
  P.report("expected object");
  return false;
}

// Allow serialization of std::optional<T> for supported T.
template <typename T> Value toJSON(const std::optional<T> &Opt) {
  return Opt ? Value(*Opt) : Value(nullptr);
}

/// Helper for mapping JSON objects onto protocol structs.
///
/// Example:
/// \code
///   bool fromJSON(const Value &E, MyStruct &R, Path P) {
///     ObjectMapper O(E, P);
///     // When returning false, error details were already reported.
///     return O && O.map("mandatory_field", R.MandatoryField) &&
///         O.mapOptional("optional_field", R.OptionalField);
///   }
/// \endcode
class ObjectMapper {
public:
  /// If O is not an object, this mapper is invalid and an error is reported.
  ObjectMapper(const Value &E, Path P) : O(E.getAsObject()), P(P) {
    if (!O)
      P.report("expected object");
  }

  /// True if the expression is an object.
  /// Must be checked before calling map().
  operator bool() const { return O; }

  /// Maps a property to a field.
  /// If the property is missing or invalid, reports an error.
  template <typename T> bool map(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    P.field(Prop).report("missing value");
    return false;
  }

  /// Maps a property to a field, if it exists.
  /// If the property exists and is invalid, reports an error.
  /// (Optional requires special handling, because missing keys are OK).
  template <typename T> bool map(StringLiteral Prop, std::optional<T> &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    Out = std::nullopt;
    return true;
  }

  /// Maps a property to a field, if it exists.
  /// If the property exists and is invalid, reports an error.
  /// If the property does not exist, Out is unchanged.
  template <typename T> bool mapOptional(StringLiteral Prop, T &Out) {
    assert(*this && "Must check this is an object before calling map()");
    if (const Value *E = O->get(Prop))
      return fromJSON(*E, Out, P.field(Prop));
    return true;
  }

private:
  const Object *O;
  Path P;
};

/// Parses the provided JSON source, or returns a ParseError.
/// The returned Value is self-contained and owns its strings (they do not refer
/// to the original source).
llvm::Expected<Value> parse(llvm::StringRef JSON);

class ParseError : public llvm::ErrorInfo<ParseError> {
  const char *Msg;
  unsigned Line, Column, Offset;

public:
  static char ID;
  ParseError(const char *Msg, unsigned Line, unsigned Column, unsigned Offset)
      : Msg(Msg), Line(Line), Column(Column), Offset(Offset) {}
  void log(llvm::raw_ostream &OS) const override {
    OS << llvm::formatv("[{0}:{1}, byte={2}]: {3}", Line, Column, Offset, Msg);
  }
  int convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode().value();
  }
};

/// Version of parse() that converts the parsed value to the type T.
/// RootName describes the root object and is used in error messages.
template <typename T>
Expected<T> parse(const llvm::StringRef &JSON, const char *RootName = "") {
  auto V = parse(JSON);
  if (!V)
    return V.takeError();
  Path::Root R(RootName);
  T Result;
  if (fromJSON(*V, Result, R))
    return std::move(Result);
  return R.getError();
}

/// json::OStream allows writing well-formed JSON without materializing
/// all structures as json::Value ahead of time.
/// It's faster, lower-level, and less safe than OS << json::Value.
/// It also allows emitting more constructs, such as comments.
///
/// Only one "top-level" object can be written to a stream.
/// Simplest usage involves passing lambdas (Blocks) to fill in containers:
///
///   json::OStream J(OS);
///   J.array([&]{
///     for (const Event &E : Events)
///       J.object([&] {
///         J.attribute("timestamp", int64_t(E.Time));
///         J.attributeArray("participants", [&] {
///           for (const Participant &P : E.Participants)
///             J.value(P.toString());
///         });
///       });
///   });
///
/// This would produce JSON like:
///
///   [
///     {
///       "timestamp": 19287398741,
///       "participants": [
///         "King Kong",
///         "Miley Cyrus",
///         "Cleopatra"
///       ]
///     },
///     ...
///   ]
///
/// The lower level begin/end methods (arrayBegin()) are more flexible but
/// care must be taken to pair them correctly:
///
///   json::OStream J(OS);
//    J.arrayBegin();
///   for (const Event &E : Events) {
///     J.objectBegin();
///     J.attribute("timestamp", int64_t(E.Time));
///     J.attributeBegin("participants");
///     for (const Participant &P : E.Participants)
///       J.value(P.toString());
///     J.attributeEnd();
///     J.objectEnd();
///   }
///   J.arrayEnd();
///
/// If the call sequence isn't valid JSON, asserts will fire in debug mode.
/// This can be mismatched begin()/end() pairs, trying to emit attributes inside
/// an array, and so on.
/// With asserts disabled, this is undefined behavior.
class OStream {
public:
  using Block = llvm::function_ref<void()>;
  // If IndentSize is nonzero, output is pretty-printed.
  explicit OStream(llvm::raw_ostream &OS, unsigned IndentSize = 0)
      : OS(OS), IndentSize(IndentSize) {
    Stack.emplace_back();
  }
  ~OStream() {
    assert(Stack.size() == 1 && "Unmatched begin()/end()");
    assert(Stack.back().Ctx == Singleton);
    assert(Stack.back().HasValue && "Did not write top-level value");
  }

  /// Flushes the underlying ostream. OStream does not buffer internally.
  void flush() { OS.flush(); }

  // High level functions to output a value.
  // Valid at top-level (exactly once), in an attribute value (exactly once),
  // or in an array (any number of times).

  /// Emit a self-contained value (number, string, vector<string> etc).
  void value(const Value &V);
  /// Emit an array whose elements are emitted in the provided Block.
  void array(Block Contents) {
    arrayBegin();
    Contents();
    arrayEnd();
  }
  /// Emit an object whose elements are emitted in the provided Block.
  void object(Block Contents) {
    objectBegin();
    Contents();
    objectEnd();
  }
  /// Emit an externally-serialized value.
  /// The caller must write exactly one valid JSON value to the provided stream.
  /// No validation or formatting of this value occurs.
  void rawValue(llvm::function_ref<void(raw_ostream &)> Contents) {
    rawValueBegin();
    Contents(OS);
    rawValueEnd();
  }
  void rawValue(llvm::StringRef Contents) {
    rawValue([&](raw_ostream &OS) { OS << Contents; });
  }
  /// Emit a JavaScript comment associated with the next printed value.
  /// The string must be valid until the next attribute or value is emitted.
  /// Comments are not part of standard JSON, and many parsers reject them!
  void comment(llvm::StringRef);

  // High level functions to output object attributes.
  // Valid only within an object (any number of times).

  /// Emit an attribute whose value is self-contained (number, vector<int> etc).
  void attribute(llvm::StringRef Key, const Value &Contents) {
    attributeImpl(Key, [&] { value(Contents); });
  }
  /// Emit an attribute whose value is an array with elements from the Block.
  void attributeArray(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { array(Contents); });
  }
  /// Emit an attribute whose value is an object with attributes from the Block.
  void attributeObject(llvm::StringRef Key, Block Contents) {
    attributeImpl(Key, [&] { object(Contents); });
  }

  // Low-level begin/end functions to output arrays, objects, and attributes.
  // Must be correctly paired. Allowed contexts are as above.

  void arrayBegin();
  void arrayEnd();
  void objectBegin();
  void objectEnd();
  void attributeBegin(llvm::StringRef Key);
  void attributeEnd();
  raw_ostream &rawValueBegin();
  void rawValueEnd();

private:
  void attributeImpl(llvm::StringRef Key, Block Contents) {
    attributeBegin(Key);
    Contents();
    attributeEnd();
  }

  void valueBegin();
  void flushComment();
  void newline();

  enum Context {
    Singleton, // Top level, or object attribute.
    Array,
    Object,
    RawValue, // External code writing a value to OS directly.
  };
  struct State {
    Context Ctx = Singleton;
    bool HasValue = false;
  };
  llvm::SmallVector<State, 16> Stack; // Never empty.
  llvm::StringRef PendingComment;
  llvm::raw_ostream &OS;
  unsigned IndentSize;
  unsigned Indent = 0;
};

/// Serializes this Value to JSON, writing it to the provided stream.
/// The formatting is compact (no extra whitespace) and deterministic.
/// For pretty-printing, use the formatv() format_provider below.
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Value &V) {
  OStream(OS).value(V);
  return OS;
}
} // namespace json

/// Allow printing json::Value with formatv().
/// The default style is basic/compact formatting, like operator<<.
/// A format string like formatv("{0:2}", Value) pretty-prints with indent 2.
template <> struct format_provider<llvm::json::Value> {
  static void format(const llvm::json::Value &, raw_ostream &, StringRef);
};
} // namespace llvm

/*== Inline implementations (moved from cpp_bridge.cpp) ==*/

namespace llvm {
namespace json {

inline Value &Object::operator[](const ObjectKey &K) {
  return try_emplace(K, 0).first->getSecond();
}
inline Value &Object::operator[](ObjectKey &&K) {
  return try_emplace(CMOVE(K), 0).first->getSecond();
}
inline Value *Object::get(StringRef K) {
  auto I = find(K);
  if (I == end())
    return 0;
  return &I->second;
}
inline const Value *Object::get(StringRef K) const {
  auto I = find(K);
  if (I == end())
    return 0;
  return &I->second;
}
inline bool Object::getNull(StringRef K) const {
  if (auto *V = get(K))
    return V->isNull();
  return false;
}
inline int Object::getBoolean(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsBoolean();
  return -1;
}
inline double Object::getNumber(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsNumber();
  return __builtin_nan("");
}
inline bool Object::getInteger(StringRef K, int64_t &Out) const {
  if (auto *V = get(K))
    return V->getAsInteger(Out);
  return false;
}
inline llvm::StringRef Object::getString(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsString();
  return llvm::StringRef();
}
inline const json::Object *Object::getObject(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsObject();
  return 0;
}
inline json::Object *Object::getObject(StringRef K) {
  if (auto *V = get(K))
    return V->getAsObject();
  return 0;
}
inline const json::Array *Object::getArray(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsArray();
  return 0;
}
inline json::Array *Object::getArray(StringRef K) {
  if (auto *V = get(K))
    return V->getAsArray();
  return 0;
}
inline bool operator==(const Object &LHS, const Object &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (const auto &L : LHS) {
    auto R = RHS.find(L.first);
    if (R == RHS.end() || L.second != R->second)
      return false;
  }
  return true;
}

inline Array::Array(init_list_t<Value> Elements) {
  V.reserve(Elements.size());
  for (const Value &V : Elements) {
    emplace_back(0);
    back().moveFrom(CMOVE(V));
  }
}

inline Value::Value(init_list_t<Value> Elements)
    : Value(json::Array(Elements)) {}

inline void Value::copyFrom(const Value &M) {
  Type = M.Type;
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
  case T_UINT64:
    memcpy(&Union, &M.Union, sizeof(Union));
    break;
  case T_StringRef:
    create<StringRef>(M.as<StringRef>());
    break;
  case T_String:
    create<SmallString<0>>(M.as<SmallString<0>>());
    break;
  case T_Object:
    create<json::Object>(M.as<json::Object>());
    break;
  case T_Array:
    create<json::Array>(M.as<json::Array>());
    break;
  }
}

inline void Value::moveFrom(const Value &&M) {
  Type = M.Type;
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
  case T_UINT64:
    memcpy(&Union, &M.Union, sizeof(Union));
    break;
  case T_StringRef:
    create<StringRef>(M.as<StringRef>());
    break;
  case T_String:
    create<SmallString<0>>(CMOVE(M.as<SmallString<0>>()));
    M.Type = T_Null;
    break;
  case T_Object:
    create<json::Object>(CMOVE(M.as<json::Object>()));
    M.Type = T_Null;
    break;
  case T_Array:
    create<json::Array>(CMOVE(M.as<json::Array>()));
    M.Type = T_Null;
    break;
  }
}

inline void Value::destroy() {
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
  case T_UINT64:
    break;
  case T_StringRef:
    as<StringRef>().~StringRef();
    break;
  case T_String:
    as<SmallString<0>>().~SmallString();
    break;
  case T_Object:
    as<json::Object>().~Object();
    break;
  case T_Array:
    as<json::Array>().~Array();
    break;
  }
}

inline bool operator==(const Value &L, const Value &R) {
  if (L.kind() != R.kind())
    return false;
  switch (L.kind()) {
  case Value::Null:
    return true;
  case Value::Boolean:
    return L.getAsBoolean() == R.getAsBoolean();
  case Value::Number:
    if (L.Type == Value::T_Integer || R.Type == Value::T_Integer) {
      int64_t LI, RI;
      bool Lok = L.getAsInteger(LI), Rok = R.getAsInteger(RI);
      return Lok && Rok && LI == RI;
    }
    return L.getAsNumber() == R.getAsNumber();
  case Value::String:
    return L.getAsString() == R.getAsString();
  case Value::Array:
    return *L.getAsArray() == *R.getAsArray();
  case Value::Object:
    return *L.getAsObject() == *R.getAsObject();
  }
  llvm_unreachable("Unknown value kind");
}

inline void Path::report(llvm::StringLiteral Msg) {
  unsigned Count = 0;
  const Path *P;
  for (P = this; P->Parent != 0; P = P->Parent)
    ++Count;
  Path::Root *R = P->Seg.root();
  R->ErrorMessage = Msg;
  R->ErrorPath.resize(Count);
  auto It = R->ErrorPath.begin();
  for (P = this; P->Parent != 0; P = P->Parent)
    *It++ = P->Seg;
}

inline Error Path::Root::getError() const {
  SmallString<256> S;
  raw_svector_ostream OS(S);
  OS << (ErrorMessage.empty() ? "invalid JSON contents" : ErrorMessage);
  if (ErrorPath.empty()) {
    if (!Name.empty())
      OS << " when parsing " << Name;
  } else {
    OS << " at " << (Name.empty() ? "(root)" : Name);
    for (const Path::Segment &S : llvm::reverse(ErrorPath)) {
      if (S.isField())
        OS << '.' << S.field();
      else
        OS << '[' << S.index() << ']';
    }
  }
  return createStringError(llvm::inconvertibleErrorCode(), OS.str());
}

namespace {

inline SmallVector<const Object::value_type *, 16>
sortedElements(const Object &O) {
  SmallVector<const Object::value_type *, 16> Elements;
  for (const auto &E : O)
    Elements.push_back(&E);
  llvm::sort(Elements,
             [](const Object::value_type *L, const Object::value_type *R) {
               return L->first < R->first;
             });
  return Elements;
}

inline void abbreviate(const Value &V, OStream &JOS) {
  switch (V.kind()) {
  case Value::Array:
    JOS.rawValue(V.getAsArray()->empty() ? "[]" : "[ ... ]");
    break;
  case Value::Object:
    JOS.rawValue(V.getAsObject()->empty() ? "{}" : "{ ... }");
    break;
  case Value::String: {
    llvm::StringRef S = V.getAsString();
    if (S.size() < 40) {
      JOS.value(V);
    } else {
      auto TruncatedStr = fixUTF8(S.take_front(37));
      TruncatedStr.append("...");
      JOS.value(TruncatedStr);
    }
    break;
  }
  default:
    JOS.value(V);
  }
}

inline void abbreviateChildren(const Value &V, OStream &JOS) {
  switch (V.kind()) {
  case Value::Array:
    JOS.array([&] {
      for (const auto &I : *V.getAsArray())
        abbreviate(I, JOS);
    });
    break;
  case Value::Object:
    JOS.object([&] {
      for (const auto *KV : sortedElements(*V.getAsObject())) {
        JOS.attributeBegin(KV->first);
        abbreviate(KV->second, JOS);
        JOS.attributeEnd();
      }
    });
    break;
  default:
    JOS.value(V);
  }
}

} // namespace

inline void Path::Root::printErrorContext(const Value &R,
                                          raw_ostream &OS) const {
  OStream JOS(OS, /*IndentSize=*/2);
  auto PrintValue = [&](const Value &V, ArrayRef<Segment> Path, auto &Recurse) {
    auto HighlightCurrent = [&] {
      SmallString<256> Comment("error: ");
      Comment += ErrorMessage;
      JOS.comment(Comment.str());
      abbreviateChildren(V, JOS);
    };
    if (Path.empty())
      return HighlightCurrent();
    const Segment &S = Path.back();
    if (S.isField()) {
      llvm::StringRef FieldName = S.field();
      const Object *O = V.getAsObject();
      if (!O || !O->get(FieldName))
        return HighlightCurrent();
      JOS.object([&] {
        for (const auto *KV : sortedElements(*O)) {
          JOS.attributeBegin(KV->first);
          if (FieldName.equals(KV->first))
            Recurse(KV->second, Path.drop_back(), Recurse);
          else
            abbreviate(KV->second, JOS);
          JOS.attributeEnd();
        }
      });
    } else {
      const Array *A = V.getAsArray();
      if (!A || S.index() >= A->size())
        return HighlightCurrent();
      JOS.array([&] {
        unsigned Current = 0;
        for (const auto &V : *A) {
          if (Current++ == S.index())
            Recurse(V, Path.drop_back(), Recurse);
          else
            abbreviate(V, JOS);
        }
      });
    }
  };
  PrintValue(R, ErrorPath, PrintValue);
}

namespace {
class Parser {
public:
  Parser(StringRef JSON)
      : Start(JSON.begin()), P(JSON.begin()), End(JSON.end()) {}

  bool checkUTF8() {
    size_t ErrOffset;
    if (isUTF8(StringRef(Start, End - Start), &ErrOffset))
      return true;
    P = Start + ErrOffset;
    return parseError("Invalid UTF-8 sequence");
  }

  bool parseValue(Value &Out);

  bool assertEnd() {
    eatWhitespace();
    if (P == End)
      return true;
    return parseError("Text after end of document");
  }

  Error takeError() {
    assert(HasErr);
    return CMOVE(Err);
  }

private:
  void eatWhitespace() {
    while (P != End && (*P == ' ' || *P == '\r' || *P == '\n' || *P == '\t'))
      ++P;
  }

  bool parseNumber(char First, Value &Out);
  bool parseString(SmallVectorImpl<char> &Out);
  bool parseUnicode(SmallVectorImpl<char> &Out);
  bool parseError(const char *Msg);

  char next() { return P == End ? 0 : *P++; }
  char peek() { return P == End ? 0 : *P; }
  static bool isNumber(char C) { return csupport_json_is_number_char(C); }

  Error Err = Error::success();
  bool HasErr = false;
  const char *Start, *P, *End;
};

inline bool Parser::parseValue(Value &Out) {
  eatWhitespace();
  if (P == End)
    return parseError("Unexpected EOF");
  switch (char C = next()) {
  case 'n':
    Out = 0;
    return (next() == 'u' && next() == 'l' && next() == 'l') ||
           parseError("Invalid JSON value (null?)");
  case 't':
    Out = true;
    return (next() == 'r' && next() == 'u' && next() == 'e') ||
           parseError("Invalid JSON value (true?)");
  case 'f':
    Out = false;
    return (next() == 'a' && next() == 'l' && next() == 's' && next() == 'e') ||
           parseError("Invalid JSON value (false?)");
  case '"': {
    SmallString<256> S;
    if (parseString(S)) {
      Out = S;
      return true;
    }
    return false;
  }
  case '[': {
    Out = Array{};
    Array &A = *Out.getAsArray();
    eatWhitespace();
    if (peek() == ']') {
      ++P;
      return true;
    }
    for (;;) {
      A.emplace_back(0);
      if (!parseValue(A.back()))
        return false;
      eatWhitespace();
      switch (next()) {
      case ',':
        eatWhitespace();
        continue;
      case ']':
        return true;
      default:
        return parseError("Expected , or ] after array element");
      }
    }
  }
  case '{': {
    Out = Object{};
    Object &O = *Out.getAsObject();
    eatWhitespace();
    if (peek() == '}') {
      ++P;
      return true;
    }
    for (;;) {
      if (next() != '"')
        return parseError("Expected object key");
      SmallString<256> K;
      if (!parseString(K))
        return false;
      eatWhitespace();
      if (next() != ':')
        return parseError("Expected : after object key");
      eatWhitespace();
      if (!parseValue(O[ObjectKey(K)]))
        return false;
      eatWhitespace();
      switch (next()) {
      case ',':
        eatWhitespace();
        continue;
      case '}':
        return true;
      default:
        return parseError("Expected , or } after object property");
      }
    }
  }
  default:
    if (isNumber(C))
      return parseNumber(C, Out);
    return parseError("Invalid JSON value");
  }
}

inline bool Parser::parseNumber(char First, Value &Out) {
  SmallString<24> S;
  S.push_back(First);
  while (isNumber(peek()))
    S.push_back(next());
  char *End;
  errno = 0;
  int64_t I = strtoll(S.c_str(), &End, 10);
  if (End == S.end() && errno != ERANGE) {
    Out = int64_t(I);
    return true;
  }
  if (First != '-') {
    errno = 0;
    uint64_t UI = strtoull(S.c_str(), &End, 10);
    if (End == S.end() && errno != ERANGE) {
      Out = UI;
      return true;
    }
  }
  Out = strtod(S.c_str(), &End);
  return End == S.end() || parseError("Invalid JSON value (number?)");
}

inline bool Parser::parseString(SmallVectorImpl<char> &Out) {
  const char *err = 0;
  const char *saved_P = P;
  char buf[4096];
  size_t n = csupport_json_parse_string_body(&P, End, buf, sizeof(buf), &err);
  if (n == (size_t)-1)
    return parseError(err);
  if (n < sizeof(buf)) {
    Out.clear();
    Out.append(buf, buf + n);
  } else {
    P = saved_P;
    char *big = (char *)malloc(n + 1);
    csupport_json_parse_string_body(&P, End, big, n + 1, &err);
    Out.clear();
    Out.append(big, big + n);
    free(big);
  }
  return true;
}

inline bool Parser::parseUnicode(SmallVectorImpl<char> &Out) {
  (void)Out;
  llvm_unreachable(
      "parseUnicode is now handled inside csupport_json_parse_string_body");
}

inline bool Parser::parseError(const char *Msg) {
  int Line, Col;
  csupport_json_calc_line_col(Start, P, &Line, &Col);
  consumeError(CMOVE(Err));
  Err = Error(uptr_t<ParseError>(new ParseError(Msg, Line, Col, P - Start)));
  HasErr = true;
  return false;
}
} // namespace

inline Expected<Value> parse(StringRef JSON) {
  Parser P(JSON);
  Value E = 0;
  if (P.checkUTF8())
    if (P.parseValue(E))
      if (P.assertEnd())
        return E;
  return P.takeError();
}

inline char ParseError::ID = 0;

inline bool isUTF8(llvm::StringRef S, size_t *ErrOffset) {
  if (LLVM_LIKELY(llvm::isASCII(S)))
    return true;

  const UTF8 *Data = (const UTF8 *)(S.data()), *Rest = Data;
  if (LLVM_LIKELY(isLegalUTF8String(&Rest, Data + S.size())))
    return true;

  if (ErrOffset)
    *ErrOffset = Rest - Data;
  return false;
}

inline SmallString<256> fixUTF8(llvm::StringRef S) {
  size_t needed = csupport_fix_utf8(S.data(), S.size(), 0, 0);
  SmallString<256> Result;
  Result.resize_for_overwrite(needed);
  csupport_fix_utf8(S.data(), S.size(), Result.data(), needed);
  return Result;
}

} // namespace json
} // namespace llvm

inline static void quote(llvm::raw_ostream &OS, llvm::StringRef S) {
  char buf[2048];
  size_t len = csupport_json_quote_to_buf(S.data(), S.size(), buf, sizeof(buf));
  OS.write(buf, len);
}

namespace llvm {
namespace json {

inline void OStream::value(const Value &V) {
  switch (V.kind()) {
  case Value::Null:
    valueBegin();
    OS << "null";
    return;
  case Value::Boolean:
    valueBegin();
    OS << (V.getAsBoolean() ? "true" : "false");
    return;
  case Value::Number:
    valueBegin();
    if (V.Type == Value::T_Integer) {
      int64_t I;
      V.getAsInteger(I);
      OS << I;
    } else if (V.Type == Value::T_UINT64) {
      uint64_t U;
      V.getAsUINT64(U);
      OS << U;
    } else
      OS << format("%.*g", 17, V.getAsNumber());
    return;
  case Value::String:
    valueBegin();
    quote(OS, V.getAsString());
    return;
  case Value::Array:
    return array([&] {
      for (const Value &E : *V.getAsArray())
        value(E);
    });
  case Value::Object:
    return object([&] {
      for (const Object::value_type *E : sortedElements(*V.getAsObject()))
        attribute(E->first, E->second);
    });
  }
}

inline void OStream::valueBegin() {
  assert(Stack.back().Ctx != Object && "Only attributes allowed here");
  if (Stack.back().HasValue) {
    assert(Stack.back().Ctx != Singleton && "Only one value allowed here");
    OS << ',';
  }
  if (Stack.back().Ctx == Array)
    newline();
  flushComment();
  Stack.back().HasValue = true;
}

inline void OStream::comment(llvm::StringRef Comment) {
  assert(PendingComment.empty() && "Only one comment per value!");
  PendingComment = Comment;
}

inline void OStream::flushComment() {
  if (PendingComment.empty())
    return;
  OS << (IndentSize ? "/* " : "/*");
  while (!PendingComment.empty()) {
    auto Pos = PendingComment.find("*/");
    if (Pos == StringRef::npos) {
      OS << PendingComment;
      PendingComment = "";
    } else {
      OS << PendingComment.take_front(Pos) << "* /";
      PendingComment = PendingComment.drop_front(Pos + 2);
    }
  }
  OS << (IndentSize ? " */" : "*/");
  if (Stack.size() > 1 && Stack.back().Ctx == Singleton) {
    if (IndentSize)
      OS << ' ';
  } else {
    newline();
  }
}

inline void OStream::newline() {
  if (IndentSize) {
    OS.write('\n');
    OS.indent(Indent);
  }
}

inline void OStream::arrayBegin() {
  valueBegin();
  Stack.emplace_back();
  Stack.back().Ctx = Array;
  Indent += IndentSize;
  OS << '[';
}

inline void OStream::arrayEnd() {
  assert(Stack.back().Ctx == Array);
  Indent -= IndentSize;
  if (Stack.back().HasValue)
    newline();
  OS << ']';
  assert(PendingComment.empty());
  Stack.pop_back();
  assert(!Stack.empty());
}

inline void OStream::objectBegin() {
  valueBegin();
  Stack.emplace_back();
  Stack.back().Ctx = Object;
  Indent += IndentSize;
  OS << '{';
}

inline void OStream::objectEnd() {
  assert(Stack.back().Ctx == Object);
  Indent -= IndentSize;
  if (Stack.back().HasValue)
    newline();
  OS << '}';
  assert(PendingComment.empty());
  Stack.pop_back();
  assert(!Stack.empty());
}

inline void OStream::attributeBegin(llvm::StringRef Key) {
  assert(Stack.back().Ctx == Object);
  if (Stack.back().HasValue)
    OS << ',';
  newline();
  flushComment();
  Stack.back().HasValue = true;
  Stack.emplace_back();
  Stack.back().Ctx = Singleton;
  if (LLVM_LIKELY(isUTF8(Key))) {
    quote(OS, Key);
  } else {
    assert(false && "Invalid UTF-8 in attribute key");
    quote(OS, fixUTF8(Key));
  }
  OS.write(':');
  if (IndentSize)
    OS.write(' ');
}

inline void OStream::attributeEnd() {
  assert(Stack.back().Ctx == Singleton);
  assert(Stack.back().HasValue && "Attribute must have a value");
  assert(PendingComment.empty());
  Stack.pop_back();
  assert(Stack.back().Ctx == Object);
}

inline raw_ostream &OStream::rawValueBegin() {
  valueBegin();
  Stack.emplace_back();
  Stack.back().Ctx = RawValue;
  return OS;
}

inline void OStream::rawValueEnd() {
  assert(Stack.back().Ctx == RawValue);
  Stack.pop_back();
}

} // namespace json
} // namespace llvm

inline void
llvm::format_provider<llvm::json::Value>::format(const llvm::json::Value &E,
                                                 llvm::raw_ostream &OS,
                                                 llvm::StringRef Options) {
  unsigned IndentAmount = 0;
  if (!Options.empty() && Options.getAsInteger(/*Radix=*/10, IndentAmount))
    llvm_unreachable("json::Value format options should be an integer");
  llvm::json::OStream(OS, IndentAmount).value(E);
}

#endif
