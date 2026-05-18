#ifndef NEVERC_LIB_SEMA_TYPE_TYPELOCBUILDER_H
#define NEVERC_LIB_SEMA_TYPE_TYPELOCBUILDER_H

#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Type/TypeLoc.h"

namespace neverc {

class TypeLocBuilder {
  enum { InlineCapacity = 8 * sizeof(SourceLocation) };

  char *Buffer;

  size_t Capacity;

  size_t Index;

#ifndef NDEBUG
  QualType LastTy;
#endif

  enum { BufferMaxAlignment = alignof(void *) };
  alignas(BufferMaxAlignment) char InlineBuffer[InlineCapacity];
  unsigned NumBytesAtAlign4;
  bool AtAlign8;

public:
  TypeLocBuilder()
      : Buffer(InlineBuffer), Capacity(InlineCapacity), Index(InlineCapacity),
        NumBytesAtAlign4(0), AtAlign8(false) {}

  ~TypeLocBuilder() {
    if (Buffer != InlineBuffer)
      delete[] Buffer;
  }

  TypeLocBuilder(const TypeLocBuilder &) = delete;
  TypeLocBuilder &operator=(const TypeLocBuilder &) = delete;

  void reserve(size_t Requested) {
    if (Requested > Capacity)
      // For now, match the request exactly.
      grow(Requested);
  }

  void pushFullCopy(TypeLoc L);

  void pushTrivial(TreeContext &Context, QualType T, SourceLocation Loc);

  TypeSpecTypeLoc pushTypeSpec(QualType T) {
    size_t LocalSize = TypeSpecTypeLoc::LocalDataSize;
    unsigned LocalAlign = TypeSpecTypeLoc::LocalDataAlignment;
    return pushImpl(T, LocalSize, LocalAlign).castAs<TypeSpecTypeLoc>();
  }

  void clear() {
#ifndef NDEBUG
    LastTy = QualType();
#endif
    Index = Capacity;
    NumBytesAtAlign4 = 0;
    AtAlign8 = false;
  }

  void TypeWasModifiedSafely(QualType T) {
#ifndef NDEBUG
    LastTy = T;
#endif
  }

  template <class TyLocType> TyLocType push(QualType T) {
    TyLocType Loc = TypeLoc(T, nullptr).castAs<TyLocType>();
    size_t LocalSize = Loc.getLocalDataSize();
    unsigned LocalAlign = Loc.getLocalDataAlignment();
    return pushImpl(T, LocalSize, LocalAlign).castAs<TyLocType>();
  }

  TypeSourceInfo *getTypeSourceInfo(TreeContext &Context, QualType T) {
#ifndef NDEBUG
    assert(T == LastTy && "type doesn't match last type pushed!");
#endif

    size_t FullDataSize = Capacity - Index;
    TypeSourceInfo *DI = Context.CreateTypeSourceInfo(T, FullDataSize);
    memcpy(DI->getTypeLoc().getOpaqueData(), &Buffer[Index], FullDataSize);
    return DI;
  }

  TypeLoc getTypeLocInContext(TreeContext &Context, QualType T) {
#ifndef NDEBUG
    assert(T == LastTy && "type doesn't match last type pushed!");
#endif

    size_t FullDataSize = Capacity - Index;
    void *Mem = Context.Allocate(FullDataSize);
    memcpy(Mem, &Buffer[Index], FullDataSize);
    return TypeLoc(T, Mem);
  }

private:
  TypeLoc pushImpl(QualType T, size_t LocalSize, unsigned LocalAlignment);

  void grow(size_t NewCapacity);

  TypeLoc getTemporaryTypeLoc(QualType T) {
#ifndef NDEBUG
    assert(LastTy == T && "type doesn't match last type pushed!");
#endif
    return TypeLoc(T, &Buffer[Index]);
  }
};

} // namespace neverc

#endif
