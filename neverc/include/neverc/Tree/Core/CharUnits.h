#ifndef NEVERC_TREE_CHARUNITS_H
#define NEVERC_TREE_CHARUNITS_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/MathExtras.h"

namespace neverc {

class CharUnits {
public:
  typedef int64_t QuantityType;

private:
  QuantityType Quantity = 0;

  explicit CharUnits(QuantityType C) : Quantity(C) {}

public:
  CharUnits() = default;

  static CharUnits Zero() { return CharUnits(0); }

  static CharUnits One() { return CharUnits(1); }

  static CharUnits fromQuantity(QuantityType Quantity) {
    return CharUnits(Quantity);
  }

  static CharUnits fromQuantity(llvm::Align Quantity) {
    return CharUnits(Quantity.value());
  }

  // Compound assignment.
  CharUnits &operator+=(const CharUnits &Other) {
    Quantity += Other.Quantity;
    return *this;
  }
  CharUnits &operator++() {
    ++Quantity;
    return *this;
  }
  CharUnits operator++(int) { return CharUnits(Quantity++); }
  CharUnits &operator-=(const CharUnits &Other) {
    Quantity -= Other.Quantity;
    return *this;
  }
  CharUnits &operator--() {
    --Quantity;
    return *this;
  }
  CharUnits operator--(int) { return CharUnits(Quantity--); }

  // Comparison operators.
  bool operator==(const CharUnits &Other) const {
    return Quantity == Other.Quantity;
  }
  bool operator!=(const CharUnits &Other) const {
    return Quantity != Other.Quantity;
  }

  // Relational operators.
  bool operator<(const CharUnits &Other) const {
    return Quantity < Other.Quantity;
  }
  bool operator<=(const CharUnits &Other) const {
    return Quantity <= Other.Quantity;
  }
  bool operator>(const CharUnits &Other) const {
    return Quantity > Other.Quantity;
  }
  bool operator>=(const CharUnits &Other) const {
    return Quantity >= Other.Quantity;
  }

  // Other predicates.

  bool isZero() const { return Quantity == 0; }

  bool isOne() const { return Quantity == 1; }

  bool isPositive() const { return Quantity > 0; }

  bool isNegative() const { return Quantity < 0; }

  bool isPowerOfTwo() const { return (Quantity & -Quantity) == Quantity; }

  bool isMultipleOf(CharUnits N) const { return (*this % N) == 0; }

  // Arithmetic operators.
  CharUnits operator*(QuantityType N) const { return CharUnits(Quantity * N); }
  CharUnits &operator*=(QuantityType N) {
    Quantity *= N;
    return *this;
  }
  CharUnits operator/(QuantityType N) const { return CharUnits(Quantity / N); }
  CharUnits &operator/=(QuantityType N) {
    Quantity /= N;
    return *this;
  }
  QuantityType operator/(const CharUnits &Other) const {
    return Quantity / Other.Quantity;
  }
  CharUnits operator%(QuantityType N) const { return CharUnits(Quantity % N); }
  QuantityType operator%(const CharUnits &Other) const {
    return Quantity % Other.Quantity;
  }
  CharUnits operator+(const CharUnits &Other) const {
    return CharUnits(Quantity + Other.Quantity);
  }
  CharUnits operator-(const CharUnits &Other) const {
    return CharUnits(Quantity - Other.Quantity);
  }
  CharUnits operator-() const { return CharUnits(-Quantity); }

  // Conversions.

  QuantityType getQuantity() const { return Quantity; }

  llvm::Align getAsAlign() const { return llvm::Align(Quantity); }

  llvm::MaybeAlign getAsMaybeAlign() const {
    return llvm::MaybeAlign(Quantity);
  }

  CharUnits alignTo(const CharUnits &Align) const {
    return CharUnits(llvm::alignTo(Quantity, Align.Quantity));
  }

  CharUnits alignmentAtOffset(CharUnits offset) const {
    assert(Quantity != 0 && "offsetting from unknown alignment?");
    return CharUnits(llvm::MinAlign(Quantity, offset.Quantity));
  }

  CharUnits alignmentOfArrayElement(CharUnits elementSize) const {
    // Since we don't track offsetted alignments, the alignment of
    // the second element (or any odd element) will be minimally
    // aligned.
    return alignmentAtOffset(elementSize);
  }

}; // class CharUnit
} // namespace neverc

inline neverc::CharUnits operator*(neverc::CharUnits::QuantityType Scale,
                                   const neverc::CharUnits &CU) {
  return CU * Scale;
}

namespace llvm {

template <> struct DenseMapInfo<neverc::CharUnits> {
  static neverc::CharUnits getEmptyKey() {
    neverc::CharUnits::QuantityType Quantity =
        DenseMapInfo<neverc::CharUnits::QuantityType>::getEmptyKey();

    return neverc::CharUnits::fromQuantity(Quantity);
  }

  static neverc::CharUnits getTombstoneKey() {
    neverc::CharUnits::QuantityType Quantity =
        DenseMapInfo<neverc::CharUnits::QuantityType>::getTombstoneKey();

    return neverc::CharUnits::fromQuantity(Quantity);
  }

  static unsigned getHashValue(const neverc::CharUnits &CU) {
    neverc::CharUnits::QuantityType Quantity = CU.getQuantity();
    return DenseMapInfo<neverc::CharUnits::QuantityType>::getHashValue(
        Quantity);
  }

  static bool isEqual(const neverc::CharUnits &LHS,
                      const neverc::CharUnits &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // NEVERC_TREE_CHARUNITS_H
