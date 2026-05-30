#include "BridgeCastHelpers.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include <cstdarg>
#include <cstdio>

using namespace llvm;

namespace neverc {
namespace plugin {

//  Sort / BSearch -- routed through host to avoid cross-CRT calls
// ===----------------------------------------------------------------------===

static void bridgeSort(void *Base, uint64_t NumElements, uint64_t ElemSize,
                       int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Base || !Cmp || NumElements <= 1 || ElemSize == 0))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return;
  std::qsort(Base, static_cast<size_t>(NumElements),
             static_cast<size_t>(ElemSize), Cmp);
}

static const void *bridgeBSearch(const void *Key, const void *Base,
                                 uint64_t NumElements, uint64_t ElemSize,
                                 int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Key || !Base || !Cmp || NumElements == 0 || ElemSize == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return nullptr;
  return std::bsearch(Key, Base, static_cast<size_t>(NumElements),
                      static_cast<size_t>(ElemSize), Cmp);
}

// ===----------------------------------------------------------------------===
//  SortCtx / BSearchCtx -- context-aware sort and binary search
//  The 3-arg comparator int(*)(a, b, ctx) lets plugin callbacks access the
//  host API vtable or pass state without hand-rolling CRT-like helpers.
//  Implemented via a thread-local thunk: SortCtx stores the user comparator
//  and context in TLS, calls std::qsort with a 2-arg shim that forwards to
//  the 3-arg function.  The TLS access is a single memory load on x86/ARM64
//  and introduces zero per-call heap allocation.
// ===----------------------------------------------------------------------===

namespace {

struct SortCtxTLS {
  int (*Cmp)(const void *, const void *, void *);
  void *Ctx;
};

static thread_local SortCtxTLS TheSortCtx;

static int sortCtxThunk(const void *A, const void *B) {
  return TheSortCtx.Cmp(A, B, TheSortCtx.Ctx);
}

struct SortCtxGuard {
  SortCtxTLS Saved;
  SortCtxGuard(int (*Cmp)(const void *, const void *, void *), void *Ctx)
      : Saved(TheSortCtx) {
    TheSortCtx.Cmp = Cmp;
    TheSortCtx.Ctx = Ctx;
  }
  ~SortCtxGuard() { TheSortCtx = Saved; }
  SortCtxGuard(const SortCtxGuard &) = delete;
  SortCtxGuard &operator=(const SortCtxGuard &) = delete;
};

} // namespace

static void bridgeSortCtx(void *Base, uint64_t NumElements, uint64_t ElemSize,
                          int (*Cmp)(const void *, const void *, void *),
                          void *Ctx) {
  if (LLVM_UNLIKELY(!Base || !Cmp || NumElements <= 1 || ElemSize == 0))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return;
  SortCtxGuard G(Cmp, Ctx);
  std::qsort(Base, static_cast<size_t>(NumElements),
             static_cast<size_t>(ElemSize), sortCtxThunk);
}

static const void *
bridgeBSearchCtx(const void *Key, const void *Base, uint64_t NumElements,
                 uint64_t ElemSize,
                 int (*Cmp)(const void *, const void *, void *), void *Ctx) {
  if (LLVM_UNLIKELY(!Key || !Base || !Cmp || NumElements == 0 || ElemSize == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(NumElements) || exceedsSizeT(ElemSize)))
    return nullptr;
  SortCtxGuard G(Cmp, Ctx);
  return std::bsearch(Key, Base, static_cast<size_t>(NumElements),
                      static_cast<size_t>(ElemSize), sortCtxThunk);
}

// ===----------------------------------------------------------------------===
//  StrFormatBuf -- snprintf to caller-owned buffer (zero allocation)
// ===----------------------------------------------------------------------===

static int bridgeStrFormatBufV(char *Buf, uint64_t BufSize, const char *Fmt,
                               va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return -1;
  if (LLVM_UNLIKELY(!Buf && BufSize > 0))
    BufSize = 0;
  size_t Sz = clampToSizeT(BufSize);
  return std::vsnprintf(Buf, Sz, Fmt, Args);
}

static int bridgeStrFormatBuf(char *Buf, uint64_t BufSize, const char *Fmt,
                              ...) {
  va_list Args;
  va_start(Args, Fmt);
  int Ret = bridgeStrFormatBufV(Buf, BufSize, Fmt, Args);
  va_end(Args);
  return Ret;
}

// ===----------------------------------------------------------------------===
//  Bounded string compare
// ===----------------------------------------------------------------------===

static int bridgeStrNCompare(const char *A, const char *B, uint64_t MaxLen) {
  if (A == B || MaxLen == 0)
    return 0;
  if (LLVM_UNLIKELY(!A))
    return -1;
  if (LLVM_UNLIKELY(!B))
    return 1;
  size_t N = clampToSizeT(MaxLen);
  return std::strncmp(A, B, N);
}

// ===----------------------------------------------------------------------===
//  StrCopyBuf -- strlcpy semantics (always null-terminates, zero allocation)
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrCopyBuf(char *Buf, uint64_t BufSize,
                                 const char *Src) {
  if (LLVM_UNLIKELY(!Src))
    return 0;
  size_t SrcLen = std::strlen(Src);
  if (BufSize > 0 && LLVM_LIKELY(Buf != nullptr)) {
    size_t CopyLen = (SrcLen < BufSize) ? SrcLen : BufSize - 1;
    std::memcpy(Buf, Src, CopyLen);
    Buf[CopyLen] = '\0';
  }
  return static_cast<uint64_t>(SrcLen);
}

// ===----------------------------------------------------------------------===
//  DynArray -- opaque growable array
//  Geometric 2x growth, contiguous buffer, cache-friendly iteration.
//  Two allocations: a small header (ElemSize/Count/Capacity/DataPtr) and the
//  data buffer itself.  Both go through the host allocator.
// ===----------------------------------------------------------------------===

namespace {
struct DynArrayImpl {
  uint64_t ElemSize;
  unsigned Count;
  unsigned Capacity;
  char *Data;
};
} // namespace

static inline DynArrayImpl *unwrapDA(NevercDynArrayRef A) {
  return reinterpret_cast<DynArrayImpl *>(A);
}
static inline NevercDynArrayRef wrapDA(DynArrayImpl *A) {
  return reinterpret_cast<NevercDynArrayRef>(A);
}

static int dynArrayGrowTo(DynArrayImpl *A, unsigned MinCapacity) {
  if (LLVM_LIKELY(MinCapacity <= A->Capacity))
    return 1;
  unsigned NewCap = A->Capacity == 0 ? 16 : A->Capacity;
  while (NewCap < MinCapacity) {
    unsigned Doubled = NewCap * 2;
    if (LLVM_UNLIKELY(Doubled <= NewCap))
      return 0;
    NewCap = Doubled;
  }
  uint64_t Bytes = static_cast<uint64_t>(NewCap) * A->ElemSize;
  if (LLVM_UNLIKELY(Bytes / A->ElemSize != NewCap))
    return 0;
  char *NewData = static_cast<char *>(bridgeRealloc(A->Data, Bytes));
  if (LLVM_UNLIKELY(!NewData))
    return 0;
  A->Data = NewData;
  A->Capacity = NewCap;
  return 1;
}

static NevercDynArrayRef bridgeDynArrayCreate(uint64_t ElemSize) {
  if (LLVM_UNLIKELY(ElemSize == 0 || ElemSize > SIZE_MAX / 16))
    return nullptr;
  auto *A = static_cast<DynArrayImpl *>(bridgeAlloc(sizeof(DynArrayImpl)));
  if (LLVM_UNLIKELY(!A))
    return nullptr;
  A->ElemSize = ElemSize;
  A->Count = 0;
  A->Capacity = 0;
  A->Data = nullptr;
  return wrapDA(A);
}

static void bridgeDynArrayDestroy(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  bridgeFree(A->Data);
  bridgeFree(A);
}

static int bridgeDynArrayPush(NevercDynArrayRef Arr, const void *Elem) {
  if (LLVM_UNLIKELY(!Arr || !Elem))
    return 0;
  auto *A = unwrapDA(Arr);
  unsigned NewCount = A->Count + 1;
  if (LLVM_UNLIKELY(NewCount == 0))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  std::memcpy(A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize, Elem,
              static_cast<size_t>(A->ElemSize));
  A->Count = NewCount;
  return 1;
}

static void *bridgeDynArrayGet(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return nullptr;
  return A->Data + static_cast<uint64_t>(Idx) * A->ElemSize;
}

static unsigned bridgeDynArrayCount(NevercDynArrayRef Arr) {
  return LLVM_LIKELY(Arr) ? unwrapDA(Arr)->Count : 0;
}

static void *bridgeDynArrayData(NevercDynArrayRef Arr) {
  return LLVM_LIKELY(Arr) ? unwrapDA(Arr)->Data : nullptr;
}

static void bridgeDynArrayClear(NevercDynArrayRef Arr) {
  if (LLVM_LIKELY(Arr))
    unwrapDA(Arr)->Count = 0;
}

static void bridgeDynArraySort(NevercDynArrayRef Arr,
                               int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Arr || !Cmp))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count > 1)
    std::qsort(A->Data, A->Count, static_cast<size_t>(A->ElemSize), Cmp);
}

static void bridgeDynArraySortCtx(
    NevercDynArrayRef Arr,
    int (*Cmp)(const void *, const void *, void *), void *Ctx) {
  if (LLVM_UNLIKELY(!Arr || !Cmp))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count <= 1)
    return;
  SortCtxGuard G(Cmp, Ctx);
  std::qsort(A->Data, A->Count, static_cast<size_t>(A->ElemSize),
             sortCtxThunk);
}

static void *bridgeDynArrayPop(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0))
    return nullptr;
  --A->Count;
  return A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize;
}

static int bridgeDynArrayReserve(NevercDynArrayRef Arr, unsigned MinCapacity) {
  if (LLVM_UNLIKELY(!Arr))
    return 0;
  auto *A = unwrapDA(Arr);
  if (A->Capacity >= MinCapacity)
    return 1;
  uint64_t Bytes = static_cast<uint64_t>(MinCapacity) * A->ElemSize;
  if (LLVM_UNLIKELY(Bytes / A->ElemSize != MinCapacity))
    return 0;
  char *NewData = static_cast<char *>(bridgeRealloc(A->Data, Bytes));
  if (LLVM_UNLIKELY(!NewData))
    return 0;
  A->Data = NewData;
  A->Capacity = MinCapacity;
  return 1;
}

static void *bridgeDynArrayDetach(NevercDynArrayRef Arr, unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Arr))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0)) {
    bridgeFree(A->Data);
    bridgeFree(A);
    return nullptr;
  }
  uint64_t ExactBytes = static_cast<uint64_t>(A->Count) * A->ElemSize;
  if (A->Count < A->Capacity) {
    char *Shrunk = static_cast<char *>(bridgeRealloc(A->Data, ExactBytes));
    if (LLVM_LIKELY(Shrunk))
      A->Data = Shrunk;
  }
  void *Result = A->Data;
  if (OutCount)
    *OutCount = A->Count;
  bridgeFree(A);
  return Result;
}

// ===----------------------------------------------------------------------===
//  StrMap -- opaque string-keyed hash table
//  Backed by LLVM's StringMap: open addressing with quadratic probing,
//  cache-friendly allocation-dense buckets, keys copied inline.
// ===----------------------------------------------------------------------===

static inline StringMap<uint64_t> *unwrapSM(NevercStrMapRef M) {
  return reinterpret_cast<StringMap<uint64_t> *>(M);
}
static inline NevercStrMapRef wrapSM(StringMap<uint64_t> *M) {
  return reinterpret_cast<NevercStrMapRef>(M);
}

static NevercStrMapRef bridgeStrMapCreate() {
  auto *M = new (std::nothrow) StringMap<uint64_t>();
  return wrapSM(M);
}

static void bridgeStrMapDestroy(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  delete unwrapSM(Map);
}

static int bridgeStrMapPut(NevercStrMapRef Map, const char *Key,
                           uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  (*unwrapSM(Map))[Key] = Value;
  return 1;
}

static int bridgeStrMapGet(NevercStrMapRef Map, const char *Key,
                           uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto *M = unwrapSM(Map);
  auto It = M->find(Key);
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeStrMapHas(NevercStrMapRef Map, const char *Key) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  return unwrapSM(Map)->count(Key) != 0;
}

static void bridgeStrMapRemove(NevercStrMapRef Map, const char *Key) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return;
  unwrapSM(Map)->erase(Key);
}

static unsigned bridgeStrMapCount(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return 0;
  return static_cast<unsigned>(unwrapSM(Map)->size());
}

static void bridgeStrMapForEach(NevercStrMapRef Map,
                                int (*Fn)(const char *, uint64_t, void *),
                                void *Ctx) {
  if (LLVM_UNLIKELY(!Map || !Fn))
    return;
  for (const auto &E : *unwrapSM(Map))
    if (Fn(E.getKeyData(), E.second, Ctx))
      break;
}

static char **bridgeStrMapCollectKeys(NevercStrMapRef Map,
                                      unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!Map || !OutCount))
    return nullptr;
  auto *M = unwrapSM(Map);
  size_t RawCount = M->size();
  if (LLVM_UNLIKELY(RawCount == 0 || RawCount > UINT_MAX))
    return nullptr;
  unsigned Count = static_cast<unsigned>(RawCount);
  auto **Keys = static_cast<char **>(
      bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(char *)));
  if (LLVM_UNLIKELY(!Keys))
    return nullptr;
  unsigned Idx = 0;
  for (const auto &E : *M) {
    size_t Len = E.getKeyLength();
    char *Key = static_cast<char *>(bridgeAlloc(Len + 1));
    if (LLVM_UNLIKELY(!Key)) {
      for (unsigned J = 0; J < Idx; ++J)
        bridgeFree(Keys[J]);
      bridgeFree(Keys);
      return nullptr;
    }
    std::memcpy(Key, E.getKeyData(), Len);
    Key[Len] = '\0';
    Keys[Idx++] = Key;
  }
  *OutCount = Idx;
  return Keys;
}

static uint64_t bridgeStrMapIncrement(NevercStrMapRef Map, const char *Key,
                                      uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto &Val = (*unwrapSM(Map))[Key];
  Val += Delta;
  return Val;
}

static void bridgeStrMapClear(NevercStrMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  unwrapSM(Map)->clear();
}

// ===----------------------------------------------------------------------===
//  N-bounded StrMap operations (key pointer + length, no null terminator)
//  Uses StringRef directly -- zero-copy lookup, key copied on insert.
// ===----------------------------------------------------------------------===

static inline StringRef toKeyRef(const char *Key, uint64_t KeyLen) {
  return StringRef(Key, clampToSizeT(KeyLen));
}

static int bridgeStrMapPutN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen, uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  (*unwrapSM(Map))[toKeyRef(Key, KeyLen)] = Value;
  return 1;
}

static int bridgeStrMapGetN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen, uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto *M = unwrapSM(Map);
  auto It = M->find(toKeyRef(Key, KeyLen));
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeStrMapHasN(NevercStrMapRef Map, const char *Key,
                            uint64_t KeyLen) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  return unwrapSM(Map)->count(toKeyRef(Key, KeyLen)) != 0;
}

static uint64_t bridgeStrMapIncrementN(NevercStrMapRef Map, const char *Key,
                                       uint64_t KeyLen, uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return 0;
  auto &Val = (*unwrapSM(Map))[toKeyRef(Key, KeyLen)];
  Val += Delta;
  return Val;
}

static void bridgeStrMapRemoveN(NevercStrMapRef Map, const char *Key,
                                uint64_t KeyLen) {
  if (LLVM_UNLIKELY(!Map || !Key))
    return;
  unwrapSM(Map)->erase(toKeyRef(Key, KeyLen));
}

// ===----------------------------------------------------------------------===
//  StrBuilder -- opaque incremental string construction
//  Backed by LLVM's SmallString<256>: inline storage avoids heap alloc
//  for strings up to 256 bytes, geometric growth beyond.
// ===----------------------------------------------------------------------===

static inline SmallString<256> *unwrapSB(NevercStrBuilderRef SB) {
  return reinterpret_cast<SmallString<256> *>(SB);
}
static inline NevercStrBuilderRef wrapSB(SmallString<256> *SB) {
  return reinterpret_cast<NevercStrBuilderRef>(SB);
}

static NevercStrBuilderRef bridgeStrBuilderCreate() {
  return wrapSB(new (std::nothrow) SmallString<256>());
}

static void bridgeStrBuilderDestroy(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return;
  delete unwrapSB(SB);
}

static void bridgeStrBuilderAppend(NevercStrBuilderRef SB, const char *S) {
  if (LLVM_UNLIKELY(!SB || !S))
    return;
  unwrapSB(SB)->append(StringRef(S));
}

static void bridgeStrBuilderAppendN(NevercStrBuilderRef SB, const char *S,
                                    uint64_t Len) {
  if (LLVM_UNLIKELY(!SB || !S))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  unwrapSB(SB)->append(StringRef(S, static_cast<size_t>(Len)));
}

static void bridgeStrBuilderAppendChar(NevercStrBuilderRef SB, char C) {
  if (LLVM_UNLIKELY(!SB))
    return;
  unwrapSB(SB)->push_back(C);
}

static void bridgeStrBuilderAppendV(NevercStrBuilderRef SB, const char *Fmt,
                                    va_list Args) {
  if (LLVM_UNLIKELY(!SB || !Fmt))
    return;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return;
  auto *B = unwrapSB(SB);
  size_t Need = static_cast<size_t>(Len);
  if (LLVM_LIKELY(Need < sizeof(Stack))) {
    B->append(StringRef(Stack, Need));
  } else {
    size_t OldSize = B->size();
    B->resize_for_overwrite(OldSize + Need + 1);
    va_list Args2;
    va_copy(Args2, Args);
    std::vsnprintf(B->data() + OldSize, Need + 1, Fmt, Args2);
    va_end(Args2);
    B->pop_back();
  }
}

static void bridgeStrBuilderAppendF(NevercStrBuilderRef SB, const char *Fmt,
                                    ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeStrBuilderAppendV(SB, Fmt, Args);
  va_end(Args);
}

static char *bridgeStrBuilderFinish(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return nullptr;
  auto *B = unwrapSB(SB);
  size_t Len = B->size();
  char *Result = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  if (Len)
    std::memcpy(Result, B->data(), Len);
  Result[Len] = '\0';
  B->clear();
  return Result;
}

static uint64_t bridgeStrBuilderLen(NevercStrBuilderRef SB) {
  return LLVM_LIKELY(SB) ? unwrapSB(SB)->size() : 0;
}

static void bridgeStrBuilderClear(NevercStrBuilderRef SB) {
  if (LLVM_LIKELY(SB))
    unwrapSB(SB)->clear();
}

static const char *bridgeStrBuilderGetStr(NevercStrBuilderRef SB) {
  if (LLVM_UNLIKELY(!SB))
    return nullptr;
  return unwrapSB(SB)->c_str();
}

// ===----------------------------------------------------------------------===
//  DynArray batch / mutate operations
// ===----------------------------------------------------------------------===

static int bridgeDynArrayPushN(NevercDynArrayRef Arr, const void *Data,
                               unsigned Count) {
  if (LLVM_UNLIKELY(!Arr))
    return 0;
  if (Count == 0)
    return 1;
  if (LLVM_UNLIKELY(!Data))
    return 0;
  auto *A = unwrapDA(Arr);
  unsigned NewCount = A->Count + Count;
  if (LLVM_UNLIKELY(NewCount < A->Count))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  std::memcpy(A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize, Data,
              static_cast<uint64_t>(Count) * A->ElemSize);
  A->Count = NewCount;
  return 1;
}

static void bridgeDynArrayRemoveSwap(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return;
  --A->Count;
  if (Idx != A->Count) {
    std::memcpy(A->Data + static_cast<uint64_t>(Idx) * A->ElemSize,
                A->Data + static_cast<uint64_t>(A->Count) * A->ElemSize,
                static_cast<size_t>(A->ElemSize));
  }
}

static void bridgeDynArrayShrinkToFit(NevercDynArrayRef Arr) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (A->Count == 0) {
    bridgeFree(A->Data);
    A->Data = nullptr;
    A->Capacity = 0;
    return;
  }
  if (A->Count == A->Capacity)
    return;
  uint64_t ExactBytes = static_cast<uint64_t>(A->Count) * A->ElemSize;
  char *Shrunk = static_cast<char *>(bridgeRealloc(A->Data, ExactBytes));
  if (LLVM_LIKELY(Shrunk)) {
    A->Data = Shrunk;
    A->Capacity = A->Count;
  }
}

// ===----------------------------------------------------------------------===
//  Order-preserving DynArray mutation: O(N) random insert and remove.
//  Use RemoveSwap when iteration order doesn't matter (O(1)).
// ===----------------------------------------------------------------------===

static int bridgeDynArrayInsert(NevercDynArrayRef Arr, unsigned Idx,
                                const void *Elem) {
  if (LLVM_UNLIKELY(!Arr || !Elem))
    return 0;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx > A->Count))
    return 0;
  unsigned NewCount = A->Count + 1;
  if (LLVM_UNLIKELY(NewCount < A->Count))
    return 0;
  if (LLVM_UNLIKELY(!dynArrayGrowTo(A, NewCount)))
    return 0;
  uint64_t Stride = A->ElemSize;
  char *Slot = A->Data + static_cast<uint64_t>(Idx) * Stride;
  if (Idx < A->Count) {
    std::memmove(Slot + Stride, Slot,
                 static_cast<size_t>((A->Count - Idx) * Stride));
  }
  std::memcpy(Slot, Elem, static_cast<size_t>(Stride));
  A->Count = NewCount;
  return 1;
}

static void bridgeDynArrayRemoveOrdered(NevercDynArrayRef Arr, unsigned Idx) {
  if (LLVM_UNLIKELY(!Arr))
    return;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(Idx >= A->Count))
    return;
  uint64_t Stride = A->ElemSize;
  unsigned Tail = A->Count - Idx - 1;
  if (Tail) {
    char *Slot = A->Data + static_cast<uint64_t>(Idx) * Stride;
    std::memmove(Slot, Slot + Stride, static_cast<size_t>(Tail * Stride));
  }
  --A->Count;
}

// ===----------------------------------------------------------------------===
//  StrMap with pre-allocated capacity
// ===----------------------------------------------------------------------===

static NevercStrMapRef bridgeStrMapCreateSized(unsigned InitialCapacity) {
  auto *M = new (std::nothrow) StringMap<uint64_t>(InitialCapacity);
  return wrapSM(M);
}

// ===----------------------------------------------------------------------===
//  IntMap -- integer-keyed hash table
//  Backed by LLVM DenseMap<uint64_t, uint64_t>: open addressing, quadratic
//  probing, cache-friendly contiguous buckets.  Sentinel keys 0xFFFF...FF
//  and 0xFFFF...FE are rejected at the API boundary.
// ===----------------------------------------------------------------------===

static constexpr uint64_t kIntMapEmptyKey = ~uint64_t(0);
static constexpr uint64_t kIntMapTombstone = ~uint64_t(0) - 1;

static inline bool isIntMapReservedKey(uint64_t Key) {
  return Key >= kIntMapTombstone;
}

static inline DenseMap<uint64_t, uint64_t> *unwrapIM(NevercIntMapRef M) {
  return reinterpret_cast<DenseMap<uint64_t, uint64_t> *>(M);
}
static inline NevercIntMapRef wrapIM(DenseMap<uint64_t, uint64_t> *M) {
  return reinterpret_cast<NevercIntMapRef>(M);
}

static NevercIntMapRef bridgeIntMapCreate() {
  auto *M = new (std::nothrow) DenseMap<uint64_t, uint64_t>();
  return wrapIM(M);
}

static void bridgeIntMapDestroy(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  delete unwrapIM(Map);
}

static int bridgeIntMapPut(NevercIntMapRef Map, uint64_t Key, uint64_t Value) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  (*unwrapIM(Map))[Key] = Value;
  return 1;
}

static int bridgeIntMapGet(NevercIntMapRef Map, uint64_t Key,
                           uint64_t *OutValue) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  auto *M = unwrapIM(Map);
  auto It = M->find(Key);
  if (It == M->end())
    return 0;
  if (OutValue)
    *OutValue = It->second;
  return 1;
}

static int bridgeIntMapHas(NevercIntMapRef Map, uint64_t Key) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  return unwrapIM(Map)->count(Key) != 0;
}

static void bridgeIntMapRemove(NevercIntMapRef Map, uint64_t Key) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return;
  unwrapIM(Map)->erase(Key);
}

static unsigned bridgeIntMapCount(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return 0;
  return static_cast<unsigned>(unwrapIM(Map)->size());
}

static uint64_t bridgeIntMapIncrement(NevercIntMapRef Map, uint64_t Key,
                                      uint64_t Delta) {
  if (LLVM_UNLIKELY(!Map || isIntMapReservedKey(Key)))
    return 0;
  auto &Val = (*unwrapIM(Map))[Key];
  Val += Delta;
  return Val;
}

static void bridgeIntMapForEach(NevercIntMapRef Map,
                                int (*Fn)(uint64_t, uint64_t, void *),
                                void *Ctx) {
  if (LLVM_UNLIKELY(!Map || !Fn))
    return;
  for (const auto &E : *unwrapIM(Map))
    if (Fn(E.first, E.second, Ctx))
      break;
}

static void bridgeIntMapClear(NevercIntMapRef Map) {
  if (LLVM_UNLIKELY(!Map))
    return;
  unwrapIM(Map)->clear();
}

static NevercIntMapRef bridgeIntMapCreateSized(unsigned InitialCapacity) {
  auto *M = new (std::nothrow) DenseMap<uint64_t, uint64_t>(InitialCapacity);
  return wrapIM(M);
}

// ===----------------------------------------------------------------------===
//  DynArray binary search -- requires prior DynArraySort with same Cmp
// ===----------------------------------------------------------------------===

static void *bridgeDynArrayBSearch(NevercDynArrayRef Arr, const void *Key,
                                   int (*Cmp)(const void *, const void *)) {
  if (LLVM_UNLIKELY(!Arr || !Key || !Cmp))
    return nullptr;
  auto *A = unwrapDA(Arr);
  if (LLVM_UNLIKELY(A->Count == 0))
    return nullptr;
  return std::bsearch(Key, A->Data, A->Count,
                      static_cast<size_t>(A->ElemSize), Cmp);
}

// ===----------------------------------------------------------------------===
//  Arena -- bump-pointer allocator for pass-scoped temporaries
//  Backed by LLVM BumpPtrAllocator: 4 KiB slab growth, sequential alloc,
//  zero per-object bookkeeping.  All memory freed in one shot by Destroy
//  or Reset.  Individual pointers MUST NOT be passed to bridgeFree.
// ===----------------------------------------------------------------------===

// ArenaImpl, unwrapArena, wrapArena defined in BridgeCastHelpers.h

static NevercArenaRef bridgeArenaCreate() {
  auto *A = new (std::nothrow) ArenaImpl();
  return wrapArena(A);
}

static void bridgeArenaDestroy(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return;
  delete unwrapArena(Arena);
}

static void *bridgeArenaAlloc(NevercArenaRef Arena, uint64_t Size) {
  if (LLVM_UNLIKELY(!Arena || Size == 0))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(Size)))
    return nullptr;
  return unwrapArena(Arena)->Alloc.Allocate(static_cast<size_t>(Size),
                                            alignof(std::max_align_t));
}

static void *bridgeArenaAllocZeroed(NevercArenaRef Arena, uint64_t Size) {
  void *P = bridgeArenaAlloc(Arena, Size);
  if (LLVM_LIKELY(P))
    std::memset(P, 0, static_cast<size_t>(Size));
  return P;
}

static void *bridgeArenaAllocArray(NevercArenaRef Arena, uint64_t Count,
                                   uint64_t ElemSize) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  return unwrapArena(Arena)->Alloc.Allocate(Total, alignof(std::max_align_t));
}

static void *bridgeArenaAllocArrayZeroed(NevercArenaRef Arena, uint64_t Count,
                                         uint64_t ElemSize) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  void *P = unwrapArena(Arena)->Alloc.Allocate(
      Total, alignof(std::max_align_t));
  if (LLVM_LIKELY(P))
    std::memset(P, 0, Total);
  return P;
}

static char *bridgeArenaStrDup(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, S, Len + 1);
  return Buf;
}

static char *bridgeArenaStrNDup(NevercArenaRef Arena, const char *S,
                                uint64_t MaxLen) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  // Cap MaxLen at SIZE_MAX-1 so CopyLen + 1 cannot overflow size_t when the
  // caller passes UINT64_MAX or SIZE_MAX as a "copy until NUL" sentinel.
  size_t Cap = (MaxLen > static_cast<uint64_t>(SIZE_MAX - 1))
                   ? (SIZE_MAX - 1)
                   : static_cast<size_t>(MaxLen);
  const void *NulPos = Cap ? std::memchr(S, '\0', Cap) : nullptr;
  size_t CopyLen = NulPos
                       ? static_cast<size_t>(
                             static_cast<const char *>(NulPos) - S)
                       : Cap;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(CopyLen + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

static void bridgeArenaReset(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return;
  unwrapArena(Arena)->Alloc.Reset();
}

static uint64_t bridgeArenaGetBytesUsed(NevercArenaRef Arena) {
  if (LLVM_UNLIKELY(!Arena))
    return 0;
  return unwrapArena(Arena)->Alloc.getBytesAllocated();
}

// ===----------------------------------------------------------------------===
//  ValueSet -- opaque hash set of NevercValueRef pointers
//  Backed by LLVM DenseSet<void *>: open addressing, quadratic probing,
//  O(1) amortized insert/contains/remove.  Sentinel pointer values
//  ((void*)-1 and (void*)-2) are never produced by the LLVM IR wrapping
//  layer, so no special-case rejection is needed beyond NULL guards.
// ===----------------------------------------------------------------------===

using ValueSetImpl = DenseSet<void *>;

static inline ValueSetImpl *unwrapVS(NevercValueSetRef S) {
  return reinterpret_cast<ValueSetImpl *>(S);
}
static inline NevercValueSetRef wrapVS(ValueSetImpl *S) {
  return reinterpret_cast<NevercValueSetRef>(S);
}

static NevercValueSetRef bridgeValueSetCreate() {
  auto *S = new (std::nothrow) ValueSetImpl();
  return wrapVS(S);
}

static NevercValueSetRef bridgeValueSetCreateSized(unsigned InitialCapacity) {
  auto *S = new (std::nothrow) ValueSetImpl(InitialCapacity);
  return wrapVS(S);
}

static void bridgeValueSetDestroy(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return;
  delete unwrapVS(Set);
}

static int bridgeValueSetInsert(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return 0;
  return unwrapVS(Set)->insert(static_cast<void *>(V)).second ? 1 : 0;
}

static int bridgeValueSetContains(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return 0;
  return unwrapVS(Set)->contains(static_cast<void *>(V)) ? 1 : 0;
}

static void bridgeValueSetRemove(NevercValueSetRef Set, NevercValueRef V) {
  if (LLVM_UNLIKELY(!Set || !V))
    return;
  unwrapVS(Set)->erase(static_cast<void *>(V));
}

static unsigned bridgeValueSetCount(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return 0;
  return static_cast<unsigned>(unwrapVS(Set)->size());
}

static void bridgeValueSetClear(NevercValueSetRef Set) {
  if (LLVM_UNLIKELY(!Set))
    return;
  unwrapVS(Set)->clear();
}

static void bridgeValueSetForEach(NevercValueSetRef Set,
                                  int (*Fn)(NevercValueRef V, void *Ctx),
                                  void *Ctx) {
  if (LLVM_UNLIKELY(!Set || !Fn))
    return;
  for (void *V : *unwrapVS(Set)) {
    if (Fn(reinterpret_cast<NevercValueRef>(V), Ctx) != 0)
      return;
  }
}

// ===----------------------------------------------------------------------===
//  Arena string concat / printf-style formatting
//  Allocates straight into the BumpPtrAllocator -- no malloc/Free pair.
//  ArenaStrFormatV uses a stack scratch buffer first; only the final
//  size hits the arena, never a heap intermediate.
// ===----------------------------------------------------------------------===

static char *bridgeArenaStrConcat(NevercArenaRef Arena, const char *L,
                                  const char *R) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  size_t LL = L ? std::strlen(L) : 0;
  size_t RL = R ? std::strlen(R) : 0;
  if (LLVM_UNLIKELY(RL > SIZE_MAX - 1 || LL > SIZE_MAX - 1 - RL))
    return nullptr;
  size_t Total = LL + RL + 1;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Total, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (LL)
    std::memcpy(Buf, L, LL);
  if (RL)
    std::memcpy(Buf + LL, R, RL);
  Buf[LL + RL] = '\0';
  return Buf;
}

static char *bridgeArenaStrFormatV(NevercArenaRef Arena, const char *Fmt,
                                   va_list Args) {
  if (LLVM_UNLIKELY(!Arena || !Fmt))
    return nullptr;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return nullptr;
  size_t Need = static_cast<size_t>(Len) + 1;
  char *Buf =
      static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(Need, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Need <= sizeof(Stack)) {
    std::memcpy(Buf, Stack, Need);
  } else {
    va_list Args2;
    va_copy(Args2, Args);
    std::vsnprintf(Buf, Need, Fmt, Args2);
    va_end(Args2);
  }
  return Buf;
}

static char *bridgeArenaStrFormat(NevercArenaRef Arena, const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Result = bridgeArenaStrFormatV(Arena, Fmt, Args);
  va_end(Args);
  return Result;
}

// ===----------------------------------------------------------------------===
//  Arena-backed string transformations -- mirrors of bridgeStrSubstring /
//  StrTrim / StrToLower / StrToUpper / StrJoin / MemDup but allocate from
//  the BumpPtrAllocator.  Saves the malloc/Free pair on plugin hot paths
//  that slice or normalize many strings.
// ===----------------------------------------------------------------------===

static char *bridgeArenaStrSubstring(NevercArenaRef Arena, const char *S,
                                     uint64_t Start, uint64_t Len) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t SLen = std::strlen(S);
  size_t CopyLen;
  if (Start >= SLen) {
    CopyLen = 0;
  } else {
    size_t Avail = SLen - static_cast<size_t>(Start);
    CopyLen = (Len > Avail) ? Avail : static_cast<size_t>(Len);
  }
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(CopyLen + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S + Start, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

static char *bridgeArenaStrTrim(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*S)))
    ++S;
  size_t Len = std::strlen(S);
  while (Len > 0 && bridgeIsWhitespace(static_cast<unsigned char>(S[Len - 1])))
    --Len;
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S, Len);
  Buf[Len] = '\0';
  return Buf;
}

// asciiToLower and asciiToUpper defined in BridgeCastHelpers.h

static char *bridgeArenaStrToLower(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    Buf[I] = static_cast<char>(
        asciiToLower(static_cast<unsigned char>(S[I])));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeArenaStrToUpper(NevercArenaRef Arena, const char *S) {
  if (LLVM_UNLIKELY(!Arena || !S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(
      unwrapArena(Arena)->Alloc.Allocate(Len + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    Buf[I] = static_cast<char>(
        asciiToUpper(static_cast<unsigned char>(S[I])));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeArenaStrJoin(NevercArenaRef Arena,
                                const char *const *Strings, unsigned Count,
                                const char *Sep) {
  if (LLVM_UNLIKELY(!Arena))
    return nullptr;
  if (Count == 0) {
    char *Empty = static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(1, 1));
    if (LLVM_LIKELY(Empty))
      *Empty = '\0';
    return Empty;
  }
  if (LLVM_UNLIKELY(!Strings))
    return nullptr;

  size_t SepLen = (Sep && *Sep) ? std::strlen(Sep) : 0;

  // Stack-cache the lengths so the second pass copies without re-walking.
  // Spills to the arena (not the host heap) when Count exceeds the cache.
  size_t StackLens[64];
  size_t *Lens = StackLens;
  if (LLVM_UNLIKELY(Count > sizeof(StackLens) / sizeof(StackLens[0]))) {
    if (LLVM_UNLIKELY(static_cast<uint64_t>(Count) >
                      SIZE_MAX / sizeof(size_t)))
      return nullptr;
    Lens = static_cast<size_t *>(unwrapArena(Arena)->Alloc.Allocate(
        static_cast<size_t>(Count) * sizeof(size_t), alignof(size_t)));
    if (LLVM_UNLIKELY(!Lens))
      return nullptr;
  }

  size_t Total = 0;
  for (unsigned I = 0; I < Count; ++I) {
    Lens[I] = Strings[I] ? std::strlen(Strings[I]) : 0;
    if (LLVM_UNLIKELY(Lens[I] > SIZE_MAX - 1 ||
                      Total > SIZE_MAX - 1 - Lens[I]))
      return nullptr;
    Total += Lens[I];
    if (I > 0 && SepLen) {
      if (LLVM_UNLIKELY(SepLen > SIZE_MAX - 1 ||
                        Total > SIZE_MAX - 1 - SepLen))
        return nullptr;
      Total += SepLen;
    }
  }

  char *Buf =
      static_cast<char *>(unwrapArena(Arena)->Alloc.Allocate(Total + 1, 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;

  char *Dst = Buf;
  for (unsigned I = 0; I < Count; ++I) {
    if (I > 0 && SepLen) {
      std::memcpy(Dst, Sep, SepLen);
      Dst += SepLen;
    }
    if (Lens[I]) {
      std::memcpy(Dst, Strings[I], Lens[I]);
      Dst += Lens[I];
    }
  }
  *Dst = '\0';
  return Buf;
}

static void *bridgeArenaMemDup(NevercArenaRef Arena, const void *Src,
                               uint64_t Len) {
  if (LLVM_UNLIKELY(!Arena || !Src))
    return nullptr;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return nullptr;
  if (Len == 0) {
    // Return a non-null 1-byte placeholder so callers can distinguish
    // "empty input" from "allocation failure".
    return unwrapArena(Arena)->Alloc.Allocate(1, 1);
  }
  size_t N = static_cast<size_t>(Len);
  void *Buf = unwrapArena(Arena)->Alloc.Allocate(N, alignof(std::max_align_t));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, Src, N);
  return Buf;
}

void populateDataStructuresBridge(NevercHostAPI &API) {
  API.Sort = bridgeSort;
  API.BSearch = bridgeBSearch;
  API.SortCtx = bridgeSortCtx;
  API.BSearchCtx = bridgeBSearchCtx;

  API.StrFormatBuf = bridgeStrFormatBuf;
  API.StrFormatBufV = bridgeStrFormatBufV;
  API.StrNCompare = bridgeStrNCompare;
  API.StrCopyBuf = bridgeStrCopyBuf;

  API.DynArrayCreate = bridgeDynArrayCreate;
  API.DynArrayDestroy = bridgeDynArrayDestroy;
  API.DynArrayPush = bridgeDynArrayPush;
  API.DynArrayGet = bridgeDynArrayGet;
  API.DynArrayCount = bridgeDynArrayCount;
  API.DynArrayData = bridgeDynArrayData;
  API.DynArrayClear = bridgeDynArrayClear;
  API.DynArraySort = bridgeDynArraySort;
  API.DynArrayPop = bridgeDynArrayPop;
  API.DynArrayReserve = bridgeDynArrayReserve;
  API.DynArrayDetach = bridgeDynArrayDetach;
  API.DynArraySortCtx = bridgeDynArraySortCtx;
  API.DynArrayPushN = bridgeDynArrayPushN;
  API.DynArrayRemoveSwap = bridgeDynArrayRemoveSwap;
  API.DynArrayShrinkToFit = bridgeDynArrayShrinkToFit;
  API.DynArrayInsert = bridgeDynArrayInsert;
  API.DynArrayRemoveOrdered = bridgeDynArrayRemoveOrdered;
  API.DynArrayBSearch = bridgeDynArrayBSearch;

  API.StrMapCreate = bridgeStrMapCreate;
  API.StrMapDestroy = bridgeStrMapDestroy;
  API.StrMapPut = bridgeStrMapPut;
  API.StrMapGet = bridgeStrMapGet;
  API.StrMapHas = bridgeStrMapHas;
  API.StrMapRemove = bridgeStrMapRemove;
  API.StrMapCount = bridgeStrMapCount;
  API.StrMapForEach = bridgeStrMapForEach;
  API.StrMapCollectKeys = bridgeStrMapCollectKeys;
  API.StrMapIncrement = bridgeStrMapIncrement;
  API.StrMapClear = bridgeStrMapClear;
  API.StrMapCreateSized = bridgeStrMapCreateSized;
  API.StrMapPutN = bridgeStrMapPutN;
  API.StrMapGetN = bridgeStrMapGetN;
  API.StrMapHasN = bridgeStrMapHasN;
  API.StrMapIncrementN = bridgeStrMapIncrementN;
  API.StrMapRemoveN = bridgeStrMapRemoveN;

  API.StrBuilderCreate = bridgeStrBuilderCreate;
  API.StrBuilderDestroy = bridgeStrBuilderDestroy;
  API.StrBuilderAppend = bridgeStrBuilderAppend;
  API.StrBuilderAppendN = bridgeStrBuilderAppendN;
  API.StrBuilderAppendChar = bridgeStrBuilderAppendChar;
  API.StrBuilderAppendF = bridgeStrBuilderAppendF;
  API.StrBuilderAppendV = bridgeStrBuilderAppendV;
  API.StrBuilderFinish = bridgeStrBuilderFinish;
  API.StrBuilderLen = bridgeStrBuilderLen;
  API.StrBuilderClear = bridgeStrBuilderClear;
  API.StrBuilderGetStr = bridgeStrBuilderGetStr;

  API.IntMapCreate = bridgeIntMapCreate;
  API.IntMapDestroy = bridgeIntMapDestroy;
  API.IntMapPut = bridgeIntMapPut;
  API.IntMapGet = bridgeIntMapGet;
  API.IntMapHas = bridgeIntMapHas;
  API.IntMapRemove = bridgeIntMapRemove;
  API.IntMapCount = bridgeIntMapCount;
  API.IntMapIncrement = bridgeIntMapIncrement;
  API.IntMapForEach = bridgeIntMapForEach;
  API.IntMapClear = bridgeIntMapClear;
  API.IntMapCreateSized = bridgeIntMapCreateSized;

  API.ArenaCreate = bridgeArenaCreate;
  API.ArenaDestroy = bridgeArenaDestroy;
  API.ArenaAlloc = bridgeArenaAlloc;
  API.ArenaAllocZeroed = bridgeArenaAllocZeroed;
  API.ArenaAllocArray = bridgeArenaAllocArray;
  API.ArenaAllocArrayZeroed = bridgeArenaAllocArrayZeroed;
  API.ArenaStrDup = bridgeArenaStrDup;
  API.ArenaStrNDup = bridgeArenaStrNDup;
  API.ArenaReset = bridgeArenaReset;
  API.ArenaGetBytesUsed = bridgeArenaGetBytesUsed;

  API.ValueSetCreate = bridgeValueSetCreate;
  API.ValueSetCreateSized = bridgeValueSetCreateSized;
  API.ValueSetDestroy = bridgeValueSetDestroy;
  API.ValueSetInsert = bridgeValueSetInsert;
  API.ValueSetContains = bridgeValueSetContains;
  API.ValueSetRemove = bridgeValueSetRemove;
  API.ValueSetCount = bridgeValueSetCount;
  API.ValueSetClear = bridgeValueSetClear;
  API.ValueSetForEach = bridgeValueSetForEach;

  API.ArenaStrConcat = bridgeArenaStrConcat;
  API.ArenaStrFormatV = bridgeArenaStrFormatV;
  API.ArenaStrFormat = bridgeArenaStrFormat;

  API.ArenaStrSubstring = bridgeArenaStrSubstring;
  API.ArenaStrTrim = bridgeArenaStrTrim;
  API.ArenaStrToLower = bridgeArenaStrToLower;
  API.ArenaStrToUpper = bridgeArenaStrToUpper;
  API.ArenaStrJoin = bridgeArenaStrJoin;
  API.ArenaMemDup = bridgeArenaMemDup;
}

} // namespace plugin
} // namespace neverc
