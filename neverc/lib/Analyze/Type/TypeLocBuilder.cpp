#include "Type/TypeLocBuilder.h"

using namespace neverc;

void TypeLocBuilder::pushFullCopy(TypeLoc L) {
  size_t Size = L.getFullDataSize();
  reserve(Size);

  llvm::SmallVector<TypeLoc, 4> TypeLocs;
  TypeLoc CurTL = L;
  while (CurTL) {
    TypeLocs.push_back(CurTL);
    CurTL = CurTL.getNextTypeLoc();
  }

  for (unsigned i = 0, e = TypeLocs.size(); i < e; ++i) {
    TypeLoc CurTL = TypeLocs[e - i - 1];
    switch (CurTL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS: {                                                       \
    CLASS##TypeLoc NewTL = push<class CLASS##TypeLoc>(CurTL.getType());        \
    memcpy(NewTL.getOpaqueData(), CurTL.getOpaqueData(),                       \
           NewTL.getLocalDataSize());                                          \
    break;                                                                     \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
    }
  }
}

void TypeLocBuilder::pushTrivial(TreeContext &Context, QualType T,
                                 SourceLocation Loc) {
  auto L = TypeLoc(T, nullptr);
  reserve(L.getFullDataSize());

  llvm::SmallVector<TypeLoc, 4> TypeLocs;
  for (auto CurTL = L; CurTL; CurTL = CurTL.getNextTypeLoc())
    TypeLocs.push_back(CurTL);

  for (const auto &CurTL : llvm::reverse(TypeLocs)) {
    switch (CurTL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case TypeLoc::CLASS: {                                                       \
    auto NewTL = push<class CLASS##TypeLoc>(CurTL.getType());                  \
    NewTL.initializeLocal(Context, Loc);                                       \
    break;                                                                     \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
    }
  }
}

void TypeLocBuilder::grow(size_t NewCapacity) {
  assert(NewCapacity > Capacity);

  // Allocate the new buffer and copy the old data into it.
  char *NewBuffer = new char[NewCapacity];
  unsigned NewIndex = Index + NewCapacity - Capacity;
  memcpy(&NewBuffer[NewIndex], &Buffer[Index], Capacity - Index);

  if (Buffer != InlineBuffer)
    delete[] Buffer;

  Buffer = NewBuffer;
  Capacity = NewCapacity;
  Index = NewIndex;
}

TypeLoc TypeLocBuilder::pushImpl(QualType T, size_t LocalSize,
                                 unsigned LocalAlignment) {
#ifndef NDEBUG
  QualType TLast = TypeLoc(T, nullptr).getNextTypeLoc().getType();
  assert(TLast == LastTy &&
         "mismatch between last type and new type's inner type");
  LastTy = T;
#endif

  assert(LocalAlignment <= BufferMaxAlignment && "Unexpected alignment");

  // If we need to grow, grow by a factor of 2.
  if (LocalSize > Index) {
    size_t RequiredCapacity = Capacity + (LocalSize - Index);
    size_t NewCapacity = Capacity * 2;
    while (RequiredCapacity > NewCapacity)
      NewCapacity *= 2;
    grow(NewCapacity);
  }

  // Because we're adding elements to the TypeLoc backwards, we have to
  // do some extra work to keep everything aligned appropriately.
  // 4 and 8 are sufficient alignment values at the moment.
  if (LocalAlignment == 4) {
    if (!AtAlign8) {
      NumBytesAtAlign4 += LocalSize;
    } else {
      unsigned Padding = NumBytesAtAlign4 % 8;
      if (Padding == 0) {
        if (LocalSize % 8 == 0) {
          // Everything is set: there's no padding and we don't need to add
          // any.
        } else {
          assert(LocalSize % 8 == 4);
          // No existing padding; add in 4 bytes padding
          memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
          Index -= 4;
        }
      } else {
        assert(Padding == 4);
        if (LocalSize % 8 == 0) {
          // Everything is set: there's 4 bytes padding and we don't need
          // to add any.
        } else {
          assert(LocalSize % 8 == 4);
          // There are 4 bytes padding, but we don't need any; remove it.
          memmove(&Buffer[Index + 4], &Buffer[Index], NumBytesAtAlign4);
          Index += 4;
        }
      }
      NumBytesAtAlign4 += LocalSize;
    }
  } else if (LocalAlignment == 8) {
    if (!AtAlign8) {
      // We have not seen any 8-byte aligned element yet. We insert a padding
      // only if the new Index is not 8-byte-aligned.
      if ((Index - LocalSize) % 8 != 0) {
        memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
        Index -= 4;
      }
    } else {
      unsigned Padding = NumBytesAtAlign4 % 8;
      if (Padding == 0) {
        if (LocalSize % 8 == 0) {
          // Everything is set: there's no padding and we don't need to add
          // any.
        } else {
          assert(LocalSize % 8 == 4);
          // No existing padding; add in 4 bytes padding
          memmove(&Buffer[Index - 4], &Buffer[Index], NumBytesAtAlign4);
          Index -= 4;
        }
      } else {
        assert(Padding == 4);
        if (LocalSize % 8 == 0) {
          // Everything is set: there's 4 bytes padding and we don't need
          // to add any.
        } else {
          assert(LocalSize % 8 == 4);
          // There are 4 bytes padding, but we don't need any; remove it.
          memmove(&Buffer[Index + 4], &Buffer[Index], NumBytesAtAlign4);
          Index += 4;
        }
      }
    }

    // Forget about any padding.
    NumBytesAtAlign4 = 0;
    AtAlign8 = true;
  } else {
    assert(LocalSize == 0);
  }

  Index -= LocalSize;

  assert(Capacity - Index == TypeLoc::getFullDataSizeForType(T) &&
         "incorrect data size provided to CreateTypeSourceInfo!");

  return getTemporaryTypeLoc(T);
}
