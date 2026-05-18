#ifndef NEVERC_CODEGEN_CONSTANTINITFUTURE_H
#define NEVERC_CODEGEN_CONSTANTINITFUTURE_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/IR/Constant.h"

// Forward-declare ConstantInitBuilderBase and give it a
// PointerLikeTypeTraits specialization so that we can safely use it
// in a PointerUnion below.
namespace neverc {
namespace Emit {
class ConstantInitBuilderBase;
}
} // namespace neverc
namespace llvm {
template <>
struct PointerLikeTypeTraits<::neverc::Emit::ConstantInitBuilderBase *> {
  using T = ::neverc::Emit::ConstantInitBuilderBase *;

  static inline void *getAsVoidPointer(T p) { return p; }
  static inline T getFromVoidPointer(void *p) { return static_cast<T>(p); }
  static constexpr int NumLowBitsAvailable = 2;
};
} // namespace llvm

namespace neverc {
namespace Emit {

class ConstantInitFuture {
  using PairTy =
      llvm::PointerUnion<ConstantInitBuilderBase *, llvm::Constant *>;

  PairTy Data;

  friend class ConstantInitBuilderBase;
  explicit ConstantInitFuture(ConstantInitBuilderBase *builder);

public:
  ConstantInitFuture() {}

  explicit ConstantInitFuture(llvm::Constant *initializer) : Data(initializer) {
    assert(initializer && "creating null future");
  }

  explicit operator bool() const { return bool(Data); }

  llvm::Type *getType() const;

  void abandon();

  void installInGlobal(llvm::GlobalVariable *global);

  void *getOpaqueValue() const { return Data.getOpaqueValue(); }
  static ConstantInitFuture getFromOpaqueValue(void *value) {
    ConstantInitFuture result;
    result.Data = PairTy::getFromOpaqueValue(value);
    return result;
  }
  static constexpr int NumLowBitsAvailable =
      llvm::PointerLikeTypeTraits<PairTy>::NumLowBitsAvailable;
};

} // end namespace Emit
} // end namespace neverc

namespace llvm {

template <> struct PointerLikeTypeTraits<::neverc::Emit::ConstantInitFuture> {
  using T = ::neverc::Emit::ConstantInitFuture;

  static inline void *getAsVoidPointer(T future) {
    return future.getOpaqueValue();
  }
  static inline T getFromVoidPointer(void *p) {
    return T::getFromOpaqueValue(p);
  }
  static constexpr int NumLowBitsAvailable = T::NumLowBitsAvailable;
};

} // end namespace llvm

#endif
