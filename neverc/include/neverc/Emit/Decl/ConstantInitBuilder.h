#ifndef NEVERC_EMIT_DECL_CONSTANTINITBUILDER_H
#define NEVERC_EMIT_DECL_CONSTANTINITBUILDER_H

#include "neverc/Emit/Decl/ConstantInitFuture.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"

#include <vector>

namespace neverc {
namespace Emit {

class ModuleEmitter;

class ConstantInitBuilderBase {
  struct SelfReference {
    llvm::GlobalVariable *Dummy;
    llvm::SmallVector<llvm::Constant *, 4> Indices;

    SelfReference(llvm::GlobalVariable *dummy) : Dummy(dummy) {}
  };
  ModuleEmitter &ME;
  llvm::SmallVector<llvm::Constant *, 16> Buffer;
  std::vector<SelfReference> SelfReferences;
  bool Frozen = false;

  friend class ConstantInitFuture;
  friend class ConstantAggregateBuilderBase;
  template <class, class> friend class ConstantAggregateBuilderTemplateBase;

protected:
  explicit ConstantInitBuilderBase(ModuleEmitter &ME) : ME(ME) {}

  ~ConstantInitBuilderBase() {
    assert(Buffer.empty() && "didn't claim all values out of buffer");
    assert(SelfReferences.empty() && "didn't apply all self-references");
  }

private:
  llvm::GlobalVariable *createGlobal(llvm::Constant *initializer,
                                     const llvm::Twine &name,
                                     CharUnits alignment, bool constant = false,
                                     llvm::GlobalValue::LinkageTypes linkage =
                                         llvm::GlobalValue::InternalLinkage,
                                     unsigned addressSpace = 0);

  ConstantInitFuture createFuture(llvm::Constant *initializer);

  void setGlobalInitializer(llvm::GlobalVariable *GV,
                            llvm::Constant *initializer);

  void resolveSelfReferences(llvm::GlobalVariable *GV);

  void abandon(size_t newEnd);
};

class ConstantAggregateBuilderBase {
protected:
  ConstantInitBuilderBase &Builder;
  ConstantAggregateBuilderBase *Parent;
  size_t Begin;
  mutable size_t CachedOffsetEnd = 0;
  bool Finished = false;
  bool Frozen = false;
  bool Packed = false;
  mutable CharUnits CachedOffsetFromGlobal;

  llvm::SmallVectorImpl<llvm::Constant *> &getBuffer() {
    return Builder.Buffer;
  }

  const llvm::SmallVectorImpl<llvm::Constant *> &getBuffer() const {
    return Builder.Buffer;
  }

  ConstantAggregateBuilderBase(ConstantInitBuilderBase &builder,
                               ConstantAggregateBuilderBase *parent)
      : Builder(builder), Parent(parent), Begin(builder.Buffer.size()) {
    if (parent) {
      assert(!parent->Frozen && "parent already has child builder active");
      parent->Frozen = true;
    } else {
      assert(!builder.Frozen && "builder already has child builder active");
      builder.Frozen = true;
    }
  }

  ~ConstantAggregateBuilderBase() {
    assert(Finished && "didn't finish aggregate builder");
  }

  void markFinished() {
    assert(!Frozen && "child builder still active");
    assert(!Finished && "builder already finished");
    Finished = true;
    if (Parent) {
      assert(Parent->Frozen && "parent not frozen while child builder active");
      Parent->Frozen = false;
    } else {
      assert(Builder.Frozen && "builder not frozen while child builder active");
      Builder.Frozen = false;
    }
  }

public:
  // Not copyable.
  ConstantAggregateBuilderBase(const ConstantAggregateBuilderBase &) = delete;
  ConstantAggregateBuilderBase &
  operator=(const ConstantAggregateBuilderBase &) = delete;

  // Movable, mostly to allow returning.  But we have to write this out
  // properly to satisfy the assert in the destructor.
  ConstantAggregateBuilderBase(ConstantAggregateBuilderBase &&other)
      : Builder(other.Builder), Parent(other.Parent), Begin(other.Begin),
        CachedOffsetEnd(other.CachedOffsetEnd), Finished(other.Finished),
        Frozen(other.Frozen), Packed(other.Packed),
        CachedOffsetFromGlobal(other.CachedOffsetFromGlobal) {
    other.Finished = true;
  }
  ConstantAggregateBuilderBase &
  operator=(ConstantAggregateBuilderBase &&other) = delete;

  size_t size() const {
    assert(!this->Finished && "cannot query after finishing builder");
    assert(!this->Frozen && "cannot query while sub-builder is active");
    assert(this->Begin <= this->getBuffer().size());
    return this->getBuffer().size() - this->Begin;
  }

  bool empty() const { return size() == 0; }

  void abandon() {
    markFinished();
    Builder.abandon(Begin);
  }

  void add(llvm::Constant *value) {
    assert(value && "adding null value to constant initializer");
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.push_back(value);
  }

  void addSize(CharUnits size);

  void addInt(llvm::IntegerType *intTy, uint64_t value, bool isSigned = false) {
    add(llvm::ConstantInt::get(intTy, value, isSigned));
  }

  void addNullPointer(llvm::PointerType *ptrTy) {
    add(llvm::ConstantPointerNull::get(ptrTy));
  }

  void addAll(llvm::ArrayRef<llvm::Constant *> values) {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.append(values.begin(), values.end());
  }

  void addRelativeOffset(llvm::IntegerType *type, llvm::Constant *target) {
    add(getRelativeOffset(type, target));
  }

  void addRelativeOffsetToPosition(llvm::IntegerType *type,
                                   llvm::Constant *target, size_t position) {
    add(getRelativeOffsetToPosition(type, target, position));
  }

  void addTaggedRelativeOffset(llvm::IntegerType *type, llvm::Constant *address,
                               unsigned tag) {
    llvm::Constant *offset = getRelativeOffset(type, address);
    if (tag) {
      offset =
          llvm::ConstantExpr::getAdd(offset, llvm::ConstantInt::get(type, tag));
    }
    add(offset);
  }

  CharUnits getNextOffsetFromGlobal() const {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    return getOffsetFromGlobalTo(Builder.Buffer.size());
  }

  class PlaceholderPosition {
    size_t Index;
    friend class ConstantAggregateBuilderBase;
    PlaceholderPosition(size_t index) : Index(index) {}
  };

  PlaceholderPosition addPlaceholder() {
    assert(!Finished && "cannot add more values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    Builder.Buffer.push_back(nullptr);
    return Builder.Buffer.size() - 1;
  }

  PlaceholderPosition addPlaceholderWithSize(llvm::Type *expectedType);

  void fillPlaceholderWithInt(PlaceholderPosition position,
                              llvm::IntegerType *type, uint64_t value,
                              bool isSigned = false) {
    fillPlaceholder(position, llvm::ConstantInt::get(type, value, isSigned));
  }

  void fillPlaceholder(PlaceholderPosition position, llvm::Constant *value) {
    assert(!Finished && "cannot change values after finishing builder");
    assert(!Frozen && "cannot add values while subbuilder is active");
    llvm::Constant *&slot = Builder.Buffer[position.Index];
    assert(slot == nullptr && "placeholder already filled");
    slot = value;
  }

  llvm::Constant *getAddrOfCurrentPosition(llvm::Type *type);

  llvm::Constant *getAddrOfPosition(llvm::Type *type, size_t position);

  llvm::ArrayRef<llvm::Constant *> getGEPIndicesToCurrentPosition(
      llvm::SmallVectorImpl<llvm::Constant *> &indices) {
    getGEPIndicesTo(indices, Builder.Buffer.size());
    return indices;
  }

protected:
  llvm::Constant *finishArray(llvm::Type *eltTy);
  llvm::Constant *finishStruct(llvm::StructType *structTy);

private:
  void getGEPIndicesTo(llvm::SmallVectorImpl<llvm::Constant *> &indices,
                       size_t position) const;

  llvm::Constant *getRelativeOffset(llvm::IntegerType *offsetType,
                                    llvm::Constant *target);

  llvm::Constant *getRelativeOffsetToPosition(llvm::IntegerType *offsetType,
                                              llvm::Constant *target,
                                              size_t position);

  CharUnits getOffsetFromGlobalTo(size_t index) const;
};

template <class Impl, class Traits>
class ConstantAggregateBuilderTemplateBase
    : public Traits::AggregateBuilderBase {
  using super = typename Traits::AggregateBuilderBase;

public:
  using InitBuilder = typename Traits::InitBuilder;
  using ArrayBuilder = typename Traits::ArrayBuilder;
  using StructBuilder = typename Traits::StructBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

protected:
  ConstantAggregateBuilderTemplateBase(InitBuilder &builder,
                                       AggregateBuilderBase *parent)
      : super(builder, parent) {}

  Impl &asImpl() { return *static_cast<Impl *>(this); }

public:
  ArrayBuilder beginArray(llvm::Type *eltTy = nullptr) {
    return ArrayBuilder(static_cast<InitBuilder &>(this->Builder), this, eltTy);
  }

  StructBuilder beginStruct(llvm::StructType *ty = nullptr) {
    return StructBuilder(static_cast<InitBuilder &>(this->Builder), this, ty);
  }

  void finishAndAddTo(AggregateBuilderBase &parent) {
    assert(this->Parent == &parent && "adding to non-parent builder");
    parent.add(asImpl().finishImpl());
  }

  template <class... As>
  llvm::GlobalVariable *finishAndCreateGlobal(As &&...args) {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.createGlobal(asImpl().finishImpl(),
                                      std::forward<As>(args)...);
  }

  void finishAndSetAsInitializer(llvm::GlobalVariable *global) {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.setGlobalInitializer(global, asImpl().finishImpl());
  }

  ConstantInitFuture finishAndCreateFuture() {
    assert(!this->Parent && "finishing non-root builder");
    return this->Builder.createFuture(asImpl().finishImpl());
  }
};

template <class Traits>
class ConstantArrayBuilderTemplateBase
    : public ConstantAggregateBuilderTemplateBase<typename Traits::ArrayBuilder,
                                                  Traits> {
  using super =
      ConstantAggregateBuilderTemplateBase<typename Traits::ArrayBuilder,
                                           Traits>;

public:
  using InitBuilder = typename Traits::InitBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

private:
  llvm::Type *EltTy;

  template <class, class> friend class ConstantAggregateBuilderTemplateBase;

protected:
  ConstantArrayBuilderTemplateBase(InitBuilder &builder,
                                   AggregateBuilderBase *parent,
                                   llvm::Type *eltTy)
      : super(builder, parent), EltTy(eltTy) {}

private:
  llvm::Constant *finishImpl() {
    return AggregateBuilderBase::finishArray(EltTy);
  }
};

template <class Traits>
class ConstantStructBuilderTemplateBase
    : public ConstantAggregateBuilderTemplateBase<
          typename Traits::StructBuilder, Traits> {
  using super =
      ConstantAggregateBuilderTemplateBase<typename Traits::StructBuilder,
                                           Traits>;

public:
  using InitBuilder = typename Traits::InitBuilder;
  using AggregateBuilderBase = typename Traits::AggregateBuilderBase;

private:
  llvm::StructType *StructTy;

  template <class, class> friend class ConstantAggregateBuilderTemplateBase;

protected:
  ConstantStructBuilderTemplateBase(InitBuilder &builder,
                                    AggregateBuilderBase *parent,
                                    llvm::StructType *structTy)
      : super(builder, parent), StructTy(structTy) {
    if (structTy)
      this->Packed = structTy->isPacked();
  }

public:
  void setPacked(bool packed) { this->Packed = packed; }

  void suggestType(llvm::StructType *structTy) {
    if (this->size() == structTy->getNumElements()) {
      StructTy = structTy;
    }
  }

private:
  llvm::Constant *finishImpl() {
    return AggregateBuilderBase::finishStruct(StructTy);
  }
};

template <class Traits>
class ConstantInitBuilderTemplateBase : public ConstantInitBuilderBase {
protected:
  ConstantInitBuilderTemplateBase(ModuleEmitter &ME)
      : ConstantInitBuilderBase(ME) {}

public:
  using InitBuilder = typename Traits::InitBuilder;
  using ArrayBuilder = typename Traits::ArrayBuilder;
  using StructBuilder = typename Traits::StructBuilder;

  ArrayBuilder beginArray(llvm::Type *eltTy = nullptr) {
    return ArrayBuilder(static_cast<InitBuilder &>(*this), nullptr, eltTy);
  }

  StructBuilder beginStruct(llvm::StructType *structTy = nullptr) {
    return StructBuilder(static_cast<InitBuilder &>(*this), nullptr, structTy);
  }
};

class ConstantInitBuilder;
class ConstantStructBuilder;
class ConstantArrayBuilder;

struct ConstantInitBuilderTraits {
  using InitBuilder = ConstantInitBuilder;
  using AggregateBuilderBase = ConstantAggregateBuilderBase;
  using ArrayBuilder = ConstantArrayBuilder;
  using StructBuilder = ConstantStructBuilder;
};

class ConstantInitBuilder
    : public ConstantInitBuilderTemplateBase<ConstantInitBuilderTraits> {
public:
  explicit ConstantInitBuilder(ModuleEmitter &ME)
      : ConstantInitBuilderTemplateBase(ME) {}
};

class ConstantArrayBuilder
    : public ConstantArrayBuilderTemplateBase<ConstantInitBuilderTraits> {
  template <class Traits> friend class ConstantInitBuilderTemplateBase;

  // The use of explicit qualification is a GCC workaround.
  template <class Impl, class Traits>
  friend class Emit::ConstantAggregateBuilderTemplateBase;

  ConstantArrayBuilder(ConstantInitBuilder &builder,
                       ConstantAggregateBuilderBase *parent, llvm::Type *eltTy)
      : ConstantArrayBuilderTemplateBase(builder, parent, eltTy) {}
};

class ConstantStructBuilder
    : public ConstantStructBuilderTemplateBase<ConstantInitBuilderTraits> {
  template <class Traits> friend class ConstantInitBuilderTemplateBase;

  // The use of explicit qualification is a GCC workaround.
  template <class Impl, class Traits>
  friend class Emit::ConstantAggregateBuilderTemplateBase;

  ConstantStructBuilder(ConstantInitBuilder &builder,
                        ConstantAggregateBuilderBase *parent,
                        llvm::StructType *structTy)
      : ConstantStructBuilderTemplateBase(builder, parent, structTy) {}
};

} // end namespace Emit
} // end namespace neverc

#endif
