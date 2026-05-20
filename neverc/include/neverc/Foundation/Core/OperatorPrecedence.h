#ifndef NEVERC_FOUNDATION_OPERATORPRECEDENCE_H
#define NEVERC_FOUNDATION_OPERATORPRECEDENCE_H

#include "neverc/Foundation/Core/TokenKinds.h"
#include "llvm/Support/Compiler.h"
#include <array>
#include <cstddef>

namespace neverc {

namespace prec {
enum Level {
  Unknown = 0,        // Not binary operator.
  Comma = 1,          // ,
  Assignment = 2,     // =, *=, /=, %=, +=, -=, <<=, >>=, &=, ^=, |=
  Conditional = 3,    // ?
  LogicalOr = 4,      // ||
  LogicalAnd = 5,     // &&
  InclusiveOr = 6,    // |
  ExclusiveOr = 7,    // ^
  And = 8,            // &
  Equality = 9,       // ==, !=
  Relational = 10,    //  >=, <=, >, <
  Shift = 11,         // <<, >>
  Additive = 12,      // -, +
  Multiplicative = 13 // *, /, %
};
}

namespace detail {
constexpr prec::Level buildPrecedenceEntry(tok::TokenKind K) {
  switch (K) {
  case tok::comma:
    return prec::Comma;
  case tok::equal:
  case tok::starequal:
  case tok::slashequal:
  case tok::percentequal:
  case tok::plusequal:
  case tok::minusequal:
  case tok::lesslessequal:
  case tok::greatergreaterequal:
  case tok::ampequal:
  case tok::caretequal:
  case tok::pipeequal:
    return prec::Assignment;
  case tok::question:
    return prec::Conditional;
  case tok::pipepipe:
    return prec::LogicalOr;
  case tok::ampamp:
    return prec::LogicalAnd;
  case tok::pipe:
    return prec::InclusiveOr;
  case tok::caret:
    return prec::ExclusiveOr;
  case tok::amp:
    return prec::And;
  case tok::exclaimequal:
  case tok::equalequal:
    return prec::Equality;
  case tok::lessequal:
  case tok::less:
  case tok::greaterequal:
  case tok::greater:
    return prec::Relational;
  case tok::lessless:
  case tok::greatergreater:
    return prec::Shift;
  case tok::plus:
  case tok::minus:
    return prec::Additive;
  case tok::percent:
  case tok::slash:
  case tok::star:
    return prec::Multiplicative;
  default:
    return prec::Unknown;
  }
}

template <size_t... I>
constexpr std::array<prec::Level, sizeof...(I)>
buildPrecedenceTable(std::index_sequence<I...>) {
  return {{buildPrecedenceEntry(static_cast<tok::TokenKind>(I))...}};
}

inline constexpr auto PrecedenceTable =
    buildPrecedenceTable(std::make_index_sequence<tok::NUM_TOKENS>{});
} // namespace detail

LLVM_ATTRIBUTE_ALWAYS_INLINE
inline prec::Level getBinOpPrecedence(tok::TokenKind Kind) {
  if (LLVM_UNLIKELY(static_cast<unsigned>(Kind) >= tok::NUM_TOKENS))
    return prec::Unknown;
  return detail::PrecedenceTable[Kind];
}

} // end namespace neverc

#endif // NEVERC_FOUNDATION_OPERATORPRECEDENCE_H
