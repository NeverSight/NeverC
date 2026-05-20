#ifndef NEVERC_LIB_EMIT_CORE_ADDRESS_H
#define NEVERC_LIB_EMIT_CORE_ADDRESS_H

#include "neverc/Tree/Core/CharUnits.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/MathExtras.h"

namespace neverc {
namespace Emit {

// Indicates whether a pointer is known not to be null.
enum KnownNonNull_t { NotKnownNonNull, KnownNonNull };

class Address {
  llvm::PointerIntPair<llvm::Value *, 1, bool> PointerAndKnownNonNull;
  llvm::Type *ElementType;
  CharUnits Alignment;

protected:
  Address(std::nullptr_t) : ElementType(nullptr) {}

public:
  Address(llvm::Value *Pointer, llvm::Type *ElementType, CharUnits Alignment,
          KnownNonNull_t IsKnownNonNull = NotKnownNonNull)
      : PointerAndKnownNonNull(Pointer, IsKnownNonNull),
        ElementType(ElementType), Alignment(Alignment) {
    assert(Pointer != nullptr && "Pointer cannot be null");
    assert(ElementType != nullptr && "Element type cannot be null");
  }

  static Address invalid() { return Address(nullptr); }
  bool isValid() const {
    return PointerAndKnownNonNull.getPointer() != nullptr;
  }

  llvm::Value *getPointer() const {
    assert(isValid());
    return PointerAndKnownNonNull.getPointer();
  }

  llvm::PointerType *getType() const {
    return llvm::cast<llvm::PointerType>(getPointer()->getType());
  }

  llvm::Type *getElementType() const {
    assert(isValid());
    return ElementType;
  }

  unsigned getAddressSpace() const { return getType()->getAddressSpace(); }

  llvm::StringRef getName() const { return getPointer()->getName(); }

  CharUnits getAlignment() const {
    assert(isValid());
    return Alignment;
  }

  Address withPointer(llvm::Value *NewPointer,
                      KnownNonNull_t IsKnownNonNull) const {
    return Address(NewPointer, getElementType(), getAlignment(),
                   IsKnownNonNull);
  }

  Address withAlignment(CharUnits NewAlignment) const {
    return Address(getPointer(), getElementType(), NewAlignment,
                   isKnownNonNull());
  }

  Address withElementType(llvm::Type *ElemTy) const {
    return Address(getPointer(), ElemTy, getAlignment(), isKnownNonNull());
  }

  KnownNonNull_t isKnownNonNull() const {
    assert(isValid());
    return (KnownNonNull_t)PointerAndKnownNonNull.getInt();
  }

  Address setKnownNonNull() {
    assert(isValid());
    PointerAndKnownNonNull.setInt(true);
    return *this;
  }
};

class ConstantAddress : public Address {
  ConstantAddress(std::nullptr_t) : Address(nullptr) {}

public:
  ConstantAddress(llvm::Constant *pointer, llvm::Type *elementType,
                  CharUnits alignment)
      : Address(pointer, elementType, alignment) {}

  static ConstantAddress invalid() { return ConstantAddress(nullptr); }

  llvm::Constant *getPointer() const {
    return llvm::cast<llvm::Constant>(Address::getPointer());
  }

  ConstantAddress withElementType(llvm::Type *ElemTy) const {
    return ConstantAddress(getPointer(), ElemTy, getAlignment());
  }

  static bool isaImpl(Address addr) {
    return llvm::isa<llvm::Constant>(addr.getPointer());
  }
  static ConstantAddress castImpl(Address addr) {
    return ConstantAddress(llvm::cast<llvm::Constant>(addr.getPointer()),
                           addr.getElementType(), addr.getAlignment());
  }
};

} // namespace Emit

// Present a minimal LLVM-like casting interface.
template <class U> inline U cast(Emit::Address addr) {
  return U::castImpl(addr);
}
template <class U> inline bool isa(Emit::Address addr) {
  return U::isaImpl(addr);
}

} // namespace neverc

#endif
