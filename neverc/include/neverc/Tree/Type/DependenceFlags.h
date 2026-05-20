#ifndef NEVERC_TREE_DEPENDENCEFLAGS_H
#define NEVERC_TREE_DEPENDENCEFLAGS_H

#include "neverc/Foundation/Core/BitmaskEnum.h"
#include "llvm/ADT/BitmaskEnum.h"
#include <cstdint>

namespace neverc {
struct ExprDependenceScope {
  enum ExprDependence : uint8_t {
    // This expr depends on an error whose resolution is unknown.
    Instantiation = 2,
    // The type of this expr depends on an error.
    Type = 4,
    // The value of this expr depends on an error.
    Value = 8,

    // NeverC extension: this expr contains or references an error, and is
    // considered dependent on how that error is resolved.
    Error = 16,

    None = 0,
    All = 30,

    TypeValue = Type | Value,
    TypeInstantiation = Type | Instantiation,
    ValueInstantiation = Value | Instantiation,
    TypeValueInstantiation = Type | Value | Instantiation,
    ErrorDependent = Error | ValueInstantiation,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Error)
  };
};
using ExprDependence = ExprDependenceScope::ExprDependence;

struct TypeDependenceScope {
  enum TypeDependence : uint8_t {
    /// Whether this type involves an error.
    Instantiation = 2,
    /// Whether this type is a dependent type or somehow involves an error
    Dependent = 4,
    /// Whether this type is a variably-modified type (C99 6.7.5).
    VariablyModified = 8,

    /// Whether this type references an error, e.g. decltype(err-expression)
    /// yields an error type.
    Error = 16,

    None = 0,
    All = 30,

    DependentInstantiation = Dependent | Instantiation,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/Error)
  };
};
using TypeDependence = TypeDependenceScope::TypeDependence;

// A combined space of all dependence concepts for all node types.
// Used when aggregating dependence of nodes of different types.
class Dependence {
public:
  enum Bits : uint8_t {
    None = 0,

    // Depends on an error in some way.
    Instantiation = 2,
    // Expression type depends on an error.
    Type = 4,
    // Expression value depends on an error.
    Value = 8,
    // Depends on an error.
    Dependent = Type | Value,
    // Includes an error, and depends on how it is resolved.
    Error = 16,
    // Type depends on a runtime value (variable-length array).
    VariablyModified = 32,

    // Dependence that is propagated syntactically, regardless of semantics.
    Syntactic = Instantiation | Error,
    // Dependence that is propagated semantically, even in cases where the
    // type doesn't syntactically appear.
    Semantic =
        Instantiation | Type | Value | Dependent | Error | VariablyModified,

    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/VariablyModified)
  };

  Dependence() : V(None) {}

  Dependence(TypeDependence D)
      : V(translate(D, TypeDependence::Instantiation, Instantiation) |
          translate(D, TypeDependence::Dependent, Dependent) |
          translate(D, TypeDependence::Error, Error) |
          translate(D, TypeDependence::VariablyModified, VariablyModified)) {}

  Dependence(ExprDependence D)
      : V(translate(D, ExprDependence::Instantiation, Instantiation) |
          translate(D, ExprDependence::Type, Type) |
          translate(D, ExprDependence::Value, Value) |
          translate(D, ExprDependence::Error, Error)) {}

  Dependence syntactic() {
    Dependence Result = *this;
    Result.V &= Syntactic;
    return Result;
  }

  Dependence semantic() {
    Dependence Result = *this;
    Result.V &= Semantic;
    return Result;
  }

  TypeDependence type() const {
    return translate(V, Instantiation, TypeDependence::Instantiation) |
           translate(V, Dependent, TypeDependence::Dependent) |
           translate(V, Error, TypeDependence::Error) |
           translate(V, VariablyModified, TypeDependence::VariablyModified);
  }

  ExprDependence expr() const {
    return translate(V, Instantiation, ExprDependence::Instantiation) |
           translate(V, Type, ExprDependence::Type) |
           translate(V, Value, ExprDependence::Value) |
           translate(V, Error, ExprDependence::Error);
  }

private:
  Bits V;

  template <typename T, typename U>
  static U translate(T Bits, T FromBit, U ToBit) {
    return (Bits & FromBit) ? ToBit : static_cast<U>(0);
  }
};

inline ExprDependence toExprDependenceForImpliedType(TypeDependence D) {
  return Dependence(D).semantic().expr();
}
inline ExprDependence toExprDependenceAsWritten(TypeDependence D) {
  return Dependence(D).expr();
}
inline ExprDependence turnTypeToValueDependence(ExprDependence D) {
  // Type-dependent expressions are always be value-dependent, so we simply drop
  // type dependency.
  return D & ~ExprDependence::Type;
}
inline ExprDependence turnValueToTypeDependence(ExprDependence D) {
  // Type-dependent expressions are always be value-dependent.
  if (D & ExprDependence::Value)
    D |= ExprDependence::Type;
  return D;
}

// Returned type-dependence will never have VariablyModified set.
inline TypeDependence toTypeDependence(ExprDependence D) {
  return Dependence(D).type();
}
inline TypeDependence toSyntacticDependence(TypeDependence D) {
  return Dependence(D).syntactic().type();
}
inline TypeDependence toSemanticDependence(TypeDependence D) {
  return Dependence(D).semantic().type();
}

LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();

} // namespace neverc
#endif
