#include "neverc/Foundation/Builtin/BuiltinString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include <cstddef>
#include <cstring>
#include <string>

using namespace neverc;
using namespace neverc::BuiltinStringNames;

namespace {

constexpr llvm::StringLiteral kFragmentType =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Type.inc"
    ;

constexpr llvm::StringLiteral kFragmentAllocation =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Allocation.inc"
    ;

constexpr llvm::StringLiteral kFragmentAccessors =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Accessors.inc"
    ;

constexpr llvm::StringLiteral kFragmentCapacity =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Capacity.inc"
    ;

constexpr llvm::StringLiteral kFragmentCompare =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Compare.inc"
    ;

constexpr llvm::StringLiteral kFragmentSearch =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Search.inc"
    ;

constexpr llvm::StringLiteral kFragmentMutation =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Mutation.inc"
    ;

constexpr llvm::StringLiteral kFragmentUtility =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Utility.inc"
    ;

constexpr llvm::StringLiteral kFragmentEncoding =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Encoding.inc"
    ;

constexpr llvm::StringLiteral kFragmentFormat =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/Format.inc"
    ;

constexpr llvm::StringLiteral kFragmentWebCodec =
#include "neverc/Foundation/Builtin/BuiltinStringPrelude/WebCodec.inc"
    ;

constexpr llvm::StringLiteral kPreludeFragments[] = {
#define NEVERC_BUILTIN_STRING_PRELUDE(Stem) kFragment##Stem,
#include "neverc/Foundation/Builtin/BuiltinStringPreludeFragments.def"
#undef NEVERC_BUILTIN_STRING_PRELUDE
};

}

namespace {

struct OverloadEntry {
  llvm::StringLiteral Method;
  unsigned Arity;
  llvm::StringLiteral FunctionName;
};

constexpr OverloadEntry kArityOverloads[] = {
#define NEVERC_BUILTIN_STRING_METHOD_OVERLOAD(method, arity, helper)           \
  {method, arity, BuiltinStringNames::helper},
#include "neverc/Foundation/Builtin/BuiltinStringMethodOverloads.def"
#undef NEVERC_BUILTIN_STRING_METHOD_OVERLOAD
};

constexpr OverloadEntry kCharOverloads[] = {
#define NEVERC_BUILTIN_STRING_METHOD_CHAR_OVERLOAD(method, arity, helper)      \
  {method, arity, BuiltinStringNames::helper},
#include "neverc/Foundation/Builtin/BuiltinStringMethodCharOverloads.def"
#undef NEVERC_BUILTIN_STRING_METHOD_CHAR_OVERLOAD
};

template <typename T, size_t N>
constexpr unsigned tableMinArity(const T (&Table)[N]) {
  unsigned M = Table[0].Arity;
  for (size_t i = 1; i < N; ++i)
    if (Table[i].Arity < M)
      M = Table[i].Arity;
  return M;
}

template <typename T, size_t N>
constexpr unsigned tableMaxArity(const T (&Table)[N]) {
  unsigned M = Table[0].Arity;
  for (size_t i = 1; i < N; ++i)
    if (Table[i].Arity > M)
      M = Table[i].Arity;
  return M;
}

constexpr unsigned kArityOvlMin = tableMinArity(kArityOverloads);
constexpr unsigned kArityOvlMax = tableMaxArity(kArityOverloads);
constexpr unsigned kCharOvlMin = tableMinArity(kCharOverloads);
constexpr unsigned kCharOvlMax = tableMaxArity(kCharOverloads);

struct DefaultEntry {
  llvm::StringLiteral Method;
  unsigned Arity;
  BuiltinString::MethodDefaultArgKind Kind;
};

constexpr DefaultEntry kDefaultTable[] = {
#define NEVERC_BUILTIN_STRING_METHOD_DEFAULT(method, arity, kind)              \
  {method, arity, BuiltinString::MethodDefaultArgKind::kind},
#include "neverc/Foundation/Builtin/BuiltinStringMethodDefaults.def"
#undef NEVERC_BUILTIN_STRING_METHOD_DEFAULT
};

}

bool BuiltinString::isMethodName(llvm::StringRef Name) {
  return llvm::StringSwitch<bool>(Name)
#define NEVERC_BUILTIN_STRING_METHOD(method, variable) .Case(method, true)
#include "neverc/Foundation/Builtin/BuiltinStringMethodNames.def"
#undef NEVERC_BUILTIN_STRING_METHOD
      .Default(false);
}

bool BuiltinString::isRuntimeFunctionName(llvm::StringRef Name) {
  if (LLVM_UNLIKELY(Name.size() < 7))
    return false;
  uint32_t Prefix4 = 0;
  std::memcpy(&Prefix4, Name.data(), 4);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr uint32_t kNeve = 'n' | ('e' << 8) | ('v' << 16) | ('e' << 24);
  constexpr uint32_t k__ne = '_' | ('_' << 8) | ('n' << 16) | ('e' << 24);
#else
  constexpr uint32_t kNeve = ('n' << 24) | ('e' << 16) | ('v' << 8) | 'e';
  constexpr uint32_t k__ne = ('_' << 24) | ('_' << 16) | ('n' << 8) | 'e';
#endif
  if (LLVM_UNLIKELY(Prefix4 == k__ne &&
                    Name.starts_with(InternalFunctionPrefix)))
    return true;
  if (LLVM_LIKELY(Prefix4 != kNeve || !Name.starts_with(PublicFunctionPrefix)))
    return false;
  return llvm::StringSwitch<bool>(Name)
#define NEVERC_BUILTIN_STRING_RUNTIME_NAME(variable)                           \
  .Case(BuiltinStringNames::variable, true)
#include "neverc/Foundation/Builtin/BuiltinStringRuntimeNames.def"
#undef NEVERC_BUILTIN_STRING_RUNTIME_NAME
      .Default(false);
}

llvm::StringRef
BuiltinString::getDefaultMethodFunctionName(llvm::StringRef Name) {
  return llvm::StringSwitch<llvm::StringRef>(Name)
#define NEVERC_BUILTIN_STRING_METHOD(method, variable)                         \
  .Case(method, BuiltinStringNames::variable)
#include "neverc/Foundation/Builtin/BuiltinStringMethodNames.def"
#undef NEVERC_BUILTIN_STRING_METHOD
      .Default({});
}

llvm::StringRef
BuiltinString::getMethodOverloadFunctionName(llvm::StringRef Name,
                                             unsigned DottedArgCount) {
  if (LLVM_UNLIKELY(DottedArgCount < kArityOvlMin ||
                    DottedArgCount > kArityOvlMax))
    return {};
  for (const auto &E : kArityOverloads) {
    if (E.Arity != DottedArgCount)
      continue;
    if (LLVM_LIKELY(E.Method.size() == Name.size()) && E.Method == Name)
      return E.FunctionName;
  }
  return {};
}

llvm::StringRef
BuiltinString::getMethodCharOverloadFunctionName(llvm::StringRef Name,
                                                 unsigned DottedArgCount) {
  if (LLVM_UNLIKELY(DottedArgCount < kCharOvlMin ||
                    DottedArgCount > kCharOvlMax))
    return {};
  for (const auto &E : kCharOverloads) {
    if (E.Arity != DottedArgCount)
      continue;
    if (LLVM_LIKELY(E.Method.size() == Name.size()) && E.Method == Name)
      return E.FunctionName;
  }
  return {};
}

llvm::StringRef BuiltinString::resolveMethodFunctionName(
    llvm::StringRef Name, unsigned DottedArgCount, bool FirstArgIsCharLike) {
  const unsigned NameLen = Name.size();
  if (LLVM_UNLIKELY(NameLen == 0))
    return {};
  const char NameFirst = Name.front();
  if (LLVM_UNLIKELY(FirstArgIsCharLike) && DottedArgCount >= kCharOvlMin &&
      DottedArgCount <= kCharOvlMax) {
    for (const auto &E : kCharOverloads) {
      if (E.Arity != DottedArgCount)
        continue;
      if (LLVM_LIKELY(E.Method.size() != NameLen ||
                      E.Method.front() != NameFirst))
        continue;
      if (E.Method == Name)
        return E.FunctionName;
    }
  }
  if (DottedArgCount >= kArityOvlMin && DottedArgCount <= kArityOvlMax) {
    for (const auto &E : kArityOverloads) {
      if (E.Arity != DottedArgCount)
        continue;
      if (LLVM_LIKELY(E.Method.size() != NameLen ||
                      E.Method.front() != NameFirst))
        continue;
      if (E.Method == Name)
        return E.FunctionName;
    }
  }
  return getDefaultMethodFunctionName(Name);
}

BuiltinString::MethodDefaultArgKind
BuiltinString::getMethodDefaultArgKind(llvm::StringRef Name,
                                       unsigned DottedArgCount) {
  constexpr unsigned MinArity = tableMinArity(kDefaultTable);
  constexpr unsigned MaxArity = tableMaxArity(kDefaultTable);
  if (DottedArgCount < MinArity || DottedArgCount > MaxArity)
    return MethodDefaultArgKind::None;
  for (const auto &E : kDefaultTable)
    if (E.Arity == DottedArgCount && E.Method == Name)
      return E.Kind;
  return MethodDefaultArgKind::None;
}

bool BuiltinString::isLValueDirectHelper(llvm::StringRef Name,
                                         bool InsideRuntime) {
  int Policy = llvm::StringSwitch<int>(Name)
#define NEVERC_BUILTIN_STRING_LVALUE_DIRECT_HELPER(NameId, InsideRuntimeOnly)  \
  .Case(BuiltinStringNames::NameId##FunctionName, InsideRuntimeOnly)
#include "neverc/Foundation/Builtin/BuiltinStringLValueDirectHelpers.def"
#undef NEVERC_BUILTIN_STRING_LVALUE_DIRECT_HELPER
                   .Default(-1);
  if (Policy < 0)
    return false;
  return Policy == 0 || InsideRuntime;
}

BuiltinString::MethodReceiverKind
BuiltinString::getMethodReceiverKind(llvm::StringRef Name) {
  return llvm::StringSwitch<MethodReceiverKind>(Name)
#define NEVERC_BUILTIN_STRING_METHOD_RECEIVER(method, kind)                    \
  .Case(method, MethodReceiverKind::kind)
#include "neverc/Foundation/Builtin/BuiltinStringMethodReceiverKinds.def"
#undef NEVERC_BUILTIN_STRING_METHOD_RECEIVER
      .Default(MethodReceiverKind::ByValue);
}

llvm::StringRef BuiltinString::getBuiltinStringPrelude() {
  static const std::string CachedPrelude = [] {
    std::size_t Total = 0;
    for (llvm::StringLiteral Fragment : kPreludeFragments)
      Total += Fragment.size();
    std::string P(Total, '\0');
    char *Dst = P.data();
    for (llvm::StringLiteral Fragment : kPreludeFragments) {
      std::memcpy(Dst, Fragment.data(), Fragment.size());
      Dst += Fragment.size();
    }
    return P;
  }();
  return CachedPrelude;
}

namespace {

constexpr llvm::StringLiteral kThinHeaderPrologue =
#include "BuiltinStringThinHeaderPrologue.inc"
    ;

constexpr llvm::StringLiteral kThinHeaderDecls =
#include "BuiltinStringThinHeaderDecls.inc"
    ;

}

llvm::StringRef BuiltinString::getBuiltinStringThinHeader() {
  static const std::string CachedThinHeader = [] {
    std::string H;
    H.reserve(kThinHeaderPrologue.size() + kThinHeaderDecls.size());
    H += kThinHeaderPrologue;
    H += kThinHeaderDecls;
    return H;
  }();
  return CachedThinHeader;
}

#include "BuiltinStringBitcode.h"

llvm::StringRef BuiltinString::getEmbeddedStringBitcode() {
  if (kStringRuntimeBitcode_len == 0)
    return {};
  return llvm::StringRef(reinterpret_cast<const char *>(kStringRuntimeBitcode),
                         kStringRuntimeBitcode_len);
}
