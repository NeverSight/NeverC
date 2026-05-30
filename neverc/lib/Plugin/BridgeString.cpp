#include "BridgeCastHelpers.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/xxhash.h"
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

using namespace llvm;

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  String utilities
// ===----------------------------------------------------------------------===

// Two-digit lookup: "00010203...9899" -- halves the number of divisions in
// integer-to-string conversion compared to single-digit extraction.
static const char kDigitPairs[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

static uint64_t bridgeStrLen(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  return std::strlen(S);
}

static char *bridgeStrDup(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, S, Len + 1);
  return Buf;
}

static char *bridgeStrConcat(const char *A, const char *B) {
  size_t LenA = A ? std::strlen(A) : 0;
  size_t LenB = B ? std::strlen(B) : 0;
  if (LLVM_UNLIKELY(LenB > SIZE_MAX - 1 || LenA > SIZE_MAX - 1 - LenB))
    return nullptr;
  size_t TotalLen = LenA + LenB;
  char *Buf = static_cast<char *>(bridgeAlloc(TotalLen + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (LenA)
    std::memcpy(Buf, A, LenA);
  if (LenB)
    std::memcpy(Buf + LenA, B, LenB);
  Buf[TotalLen] = '\0';
  return Buf;
}

static int bridgeStrEqual(const char *A, const char *B) {
  if (A == B)
    return 1;
  if (LLVM_UNLIKELY(!A || !B))
    return 0;
  return std::strcmp(A, B) == 0;
}

static char *bridgeIntToStr(int64_t Val) {
  char Tmp[21];
  char *End = Tmp + 20;
  *End = '\0';
  bool Neg = Val < 0;
  uint64_t U = Neg ? -static_cast<uint64_t>(Val) : static_cast<uint64_t>(Val);

  char *P = End;
  while (U >= 100) {
    unsigned Idx = static_cast<unsigned>(U % 100) * 2;
    U /= 100;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  }
  if (U >= 10) {
    unsigned Idx = static_cast<unsigned>(U) * 2;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  } else {
    *--P = static_cast<char>('0' + U);
  }
  if (Neg)
    *--P = '-';

  size_t Len = static_cast<size_t>(End - P);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, P, Len + 1);
  return Buf;
}

static char *bridgeUIntToStr(uint64_t Val) {
  char Tmp[21];
  char *End = Tmp + 20;
  *End = '\0';

  char *P = End;
  while (Val >= 100) {
    unsigned Idx = static_cast<unsigned>(Val % 100) * 2;
    Val /= 100;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  }
  if (Val >= 10) {
    unsigned Idx = static_cast<unsigned>(Val) * 2;
    *--P = kDigitPairs[Idx + 1];
    *--P = kDigitPairs[Idx];
  } else {
    *--P = static_cast<char>('0' + Val);
  }

  size_t Len = static_cast<size_t>(End - P);
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  std::memcpy(Buf, P, Len + 1);
  return Buf;
}

static char *bridgeStrFormatV(const char *Fmt, va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return nullptr;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return nullptr;
  size_t Need = static_cast<size_t>(Len) + 1;
  char *Buf = static_cast<char *>(bridgeAlloc(Need));
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

static char *bridgeStrFormat(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  char *Result = bridgeStrFormatV(Fmt, Args);
  va_end(Args);
  return Result;
}

// ===----------------------------------------------------------------------===
//  Raw memory utilities
// ===----------------------------------------------------------------------===

static void bridgeMemCopy(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Src && Len))
    std::memcpy(Dst, Src, static_cast<size_t>(Len));
}

static void bridgeMemSet(void *Dst, int Val, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Len))
    std::memset(Dst, Val, static_cast<size_t>(Len));
}

static void bridgeMemMove(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Src && Len))
    std::memmove(Dst, Src, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Byte-reverse copy.  Two supported modes:
//    1. Dst == Src           -- in-place reversal (meet-in-the-middle swap)
//    2. Dst and Src disjoint -- forward reverse-copy
//  Partial overlap (Dst != Src but ranges intersect) is undefined behaviour
//  per the API contract and treated as a no-op here, matching how the libc
//  memcpy contract handles overlapping regions.  Caller must use a separate
//  buffer for that case.
// ===----------------------------------------------------------------------===

static void bridgeMemReverse(void *Dst, const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(!Dst || !Src || Len < 2))
    return;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  auto *D = static_cast<unsigned char *>(Dst);
  const auto *S = static_cast<const unsigned char *>(Src);
  size_t N = static_cast<size_t>(Len);
  if (D == S) {
    for (size_t I = 0, J = N - 1; I < J; ++I, --J) {
      unsigned char T = D[I];
      D[I] = D[J];
      D[J] = T;
    }
    return;
  }
  // Reject partial overlap (UB per contract).  Pointer comparison across
  // distinct objects is implementation-defined but works on every platform
  // we ship for, and a false positive here just costs the caller the data
  // they would have read anyway.
  if (LLVM_UNLIKELY(D + N > S && S + N > D))
    return;
  for (size_t I = 0; I < N; ++I)
    D[I] = S[N - 1 - I];
}

static int bridgeMemCompare(const void *A, const void *B, uint64_t Len) {
  if (LLVM_UNLIKELY(!A || !B || !Len))
    return 0;
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return 0;
  return std::memcmp(A, B, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Formatted diagnostics (printf-style, one-call format + emit)
// ===----------------------------------------------------------------------===

static void bridgeDiagV(void (*Emit)(const char *), const char *Fmt,
                        va_list Args) {
  if (LLVM_UNLIKELY(!Fmt))
    return;
  char Stack[1024];
  va_list ArgsCopy;
  va_copy(ArgsCopy, Args);
  int Len = std::vsnprintf(Stack, sizeof(Stack), Fmt, ArgsCopy);
  va_end(ArgsCopy);
  if (LLVM_UNLIKELY(Len < 0))
    return;
  size_t Need = static_cast<size_t>(Len) + 1;
  if (Need <= sizeof(Stack)) {
    Emit(Stack);
    return;
  }
  char *Heap = static_cast<char *>(bridgeAlloc(Need));
  if (LLVM_UNLIKELY(!Heap))
    return;
  va_list Args2;
  va_copy(Args2, Args);
  std::vsnprintf(Heap, Need, Fmt, Args2);
  va_end(Args2);
  Emit(Heap);
  bridgeFree(Heap);
}

static void bridgeDiagNoteV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagNote, Fmt, Args);
}

static void bridgeDiagWarningV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagWarning, Fmt, Args);
}

static void bridgeDiagErrorV(const char *Fmt, va_list Args) {
  bridgeDiagV(bridgeDiagError, Fmt, Args);
}

static void bridgeDiagNoteF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagNoteV(Fmt, Args);
  va_end(Args);
}

static void bridgeDiagWarningF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagWarningV(Fmt, Args);
  va_end(Args);
}

static void bridgeDiagErrorF(const char *Fmt, ...) {
  va_list Args;
  va_start(Args, Fmt);
  bridgeDiagErrorV(Fmt, Args);
  va_end(Args);
}

// ===----------------------------------------------------------------------===
//  Zero-initialized allocation
// ===----------------------------------------------------------------------===

static void *bridgeAllocZeroed(uint64_t Count, uint64_t ElemSize) {
  if (LLVM_UNLIKELY(exceedsSizeT(Count) || exceedsSizeT(ElemSize)))
    return nullptr;
  if (LLVM_UNLIKELY(ElemSize && Count > SIZE_MAX / ElemSize))
    return nullptr;
  return ::calloc(static_cast<size_t>(Count), static_cast<size_t>(ElemSize));
}

// ===----------------------------------------------------------------------===
//  Extended string operations
// ===----------------------------------------------------------------------===

static int bridgeStrStartsWith(const char *S, const char *Prefix) {
  if (LLVM_UNLIKELY(!S || !Prefix))
    return 0;
  for (;; ++S, ++Prefix) {
    if (*Prefix == '\0')
      return 1;
    if (*S != *Prefix)
      return 0;
  }
}

static int bridgeStrEndsWith(const char *S, const char *Suffix) {
  if (LLVM_UNLIKELY(!S || !Suffix))
    return 0;
  size_t SLen = std::strlen(S);
  size_t XLen = std::strlen(Suffix);
  if (XLen > SLen)
    return 0;
  return std::memcmp(S + SLen - XLen, Suffix, XLen) == 0;
}

static int bridgeStrContains(const char *Haystack, const char *Needle) {
  if (LLVM_UNLIKELY(!Haystack))
    return 0;
  if (LLVM_UNLIKELY(!Needle || !*Needle))
    return 1;
  return std::strstr(Haystack, Needle) != nullptr;
}

static int bridgeStrCompare(const char *A, const char *B) {
  if (A == B)
    return 0;
  if (LLVM_UNLIKELY(!A))
    return -1;
  if (LLVM_UNLIKELY(!B))
    return 1;
  return std::strcmp(A, B);
}

// bridgeIsWhitespace defined in BridgeCastHelpers.h

static int bridgeStrToInt64(const char *S, int64_t *Out) {
  if (LLVM_UNLIKELY(!S || !Out))
    return 0;
  char *End = nullptr;
  errno = 0;
  long long Val = std::strtoll(S, &End, 10);
  if (errno != 0 || End == S || *End != '\0')
    return 0;
  *Out = static_cast<int64_t>(Val);
  return 1;
}

static int bridgeStrToUInt64(const char *S, uint64_t *Out) {
  if (LLVM_UNLIKELY(!S || !Out))
    return 0;
  const char *P = S;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*P)))
    ++P;
  if (*P == '-')
    return 0;
  char *End = nullptr;
  errno = 0;
  unsigned long long Val = std::strtoull(P, &End, 10);
  if (errno != 0 || End == P || *End != '\0')
    return 0;
  *Out = static_cast<uint64_t>(Val);
  return 1;
}

static char *bridgeStrSubstring(const char *S, uint64_t Start, uint64_t Len) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t SLen = std::strlen(S);
  if (Start >= SLen)
    Len = 0;
  else if (Len > SLen - Start)
    Len = SLen - Start;
  char *Buf = static_cast<char *>(bridgeAlloc(static_cast<size_t>(Len) + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S + Start, static_cast<size_t>(Len));
  Buf[Len] = '\0';
  return Buf;
}

static char *bridgeStrTrim(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  while (bridgeIsWhitespace(static_cast<unsigned char>(*S)))
    ++S;
  size_t Len = std::strlen(S);
  while (Len > 0 && bridgeIsWhitespace(static_cast<unsigned char>(S[Len - 1])))
    --Len;
  char *Buf = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (Len)
    std::memcpy(Buf, S, Len);
  Buf[Len] = '\0';
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Convenience zero-fill
// ===----------------------------------------------------------------------===

static void bridgeMemZero(void *Dst, uint64_t Len) {
  if (LLVM_UNLIKELY(exceedsSizeT(Len)))
    return;
  if (LLVM_LIKELY(Dst && Len))
    std::memset(Dst, 0, static_cast<size_t>(Len));
}

// ===----------------------------------------------------------------------===
//  Character search
// ===----------------------------------------------------------------------===

// Reject C values that collapse to '\0' after the implicit (char) truncation
// performed by std::strchr/strrchr -- otherwise StrFindChar(s, 256) would
// "find" the null terminator and report strlen(s) instead of NotFound.
static uint64_t bridgeStrFindChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return UINT64_MAX;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return UINT64_MAX;
  const char *P = std::strchr(S, Needle);
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(P - S);
}

static uint64_t bridgeStrFindLastChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return UINT64_MAX;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return UINT64_MAX;
  const char *P = std::strrchr(S, Needle);
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(P - S);
}

// ===----------------------------------------------------------------------===
//  Overflow-safe array reallocation
// ===----------------------------------------------------------------------===

static void *bridgeReallocArray(void *Ptr, uint64_t Count, uint64_t ElemSize) {
  size_t Total = checkedArraySize(Count, ElemSize);
  if (LLVM_UNLIKELY(Total == 0 && Count != 0 && ElemSize != 0))
    return nullptr;
  if (Total == 0)
    Total = 1;
  return bridgeRealloc(Ptr, Total);
}

//  StrJoin -- concatenate an array of strings with a separator
// ===----------------------------------------------------------------------===

static char *bridgeStrJoin(const char *const *Strings, unsigned Count,
                           const char *Sep) {
  if (Count == 0) {
    char *Empty = static_cast<char *>(bridgeAlloc(1));
    if (Empty)
      Empty[0] = '\0';
    return Empty;
  }
  if (LLVM_UNLIKELY(!Strings))
    return nullptr;

  size_t SepLen = (Sep && *Sep) ? std::strlen(Sep) : 0;

  size_t StackLens[64];
  size_t *Lens = StackLens;
  if (LLVM_UNLIKELY(Count > 64)) {
    Lens = static_cast<size_t *>(
        bridgeAlloc(static_cast<uint64_t>(Count) * sizeof(size_t)));
    if (LLVM_UNLIKELY(!Lens))
      return nullptr;
  }

  size_t Total = 0;
  for (unsigned I = 0; I < Count; ++I) {
    Lens[I] = Strings[I] ? std::strlen(Strings[I]) : 0;
    // Two-step overflow guard: catch the case where Lens[I] itself is
    // pathologically close to SIZE_MAX before computing SIZE_MAX-1-Lens[I],
    // which would otherwise wrap around for Lens[I] >= SIZE_MAX-1.
    if (LLVM_UNLIKELY(Lens[I] > SIZE_MAX - 1 ||
                      Total > SIZE_MAX - 1 - Lens[I])) {
      if (Lens != StackLens)
        bridgeFree(Lens);
      return nullptr;
    }
    Total += Lens[I];
    if (I > 0 && SepLen) {
      if (LLVM_UNLIKELY(SepLen > SIZE_MAX - 1 ||
                        Total > SIZE_MAX - 1 - SepLen)) {
        if (Lens != StackLens)
          bridgeFree(Lens);
        return nullptr;
      }
      Total += SepLen;
    }
  }

  char *Buf = static_cast<char *>(bridgeAlloc(Total + 1));
  if (LLVM_UNLIKELY(!Buf)) {
    if (Lens != StackLens)
      bridgeFree(Lens);
    return nullptr;
  }

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

  if (Lens != StackLens)
    bridgeFree(Lens);
  return Buf;
}

// ===----------------------------------------------------------------------===
//  StrSplit -- split a string by delimiter into an array of strings
// ===----------------------------------------------------------------------===

static char **bridgeStrSplit(const char *S, const char *Delim,
                             unsigned *OutCount) {
  if (OutCount)
    *OutCount = 0;
  if (LLVM_UNLIKELY(!S || !OutCount))
    return nullptr;

  if (LLVM_UNLIKELY(!Delim || !*Delim)) {
    char **Arr = static_cast<char **>(bridgeAlloc(sizeof(char *)));
    if (LLVM_UNLIKELY(!Arr))
      return nullptr;
    Arr[0] = bridgeStrDup(S);
    if (LLVM_UNLIKELY(!Arr[0])) {
      bridgeFree(Arr);
      return nullptr;
    }
    *OutCount = 1;
    return Arr;
  }

  size_t DelimLen = std::strlen(Delim);

  // Single pass: collect delimiter hit offsets into a stack-local buffer,
  // spilling to the heap only when there are many hits.  This avoids
  // scanning the string twice (count pass + split pass).
  size_t StackHits[64];
  size_t *Hits = StackHits;
  size_t HitCap = sizeof(StackHits) / sizeof(StackHits[0]);
  size_t HitCount = 0;

  const char *P = S;
  while ((P = std::strstr(P, Delim)) != nullptr) {
    if (HitCount == HitCap) {
      if (HitCap > (SIZE_MAX / sizeof(size_t)) / 2) {
        if (Hits != StackHits)
          bridgeFree(Hits);
        return nullptr;
      }
      size_t NewCap = HitCap * 2;
      size_t *NewBuf;
      if (Hits == StackHits) {
        NewBuf =
            static_cast<size_t *>(bridgeAlloc(NewCap * sizeof(size_t)));
        if (LLVM_LIKELY(NewBuf))
          std::memcpy(NewBuf, StackHits, HitCount * sizeof(size_t));
      } else {
        NewBuf =
            static_cast<size_t *>(bridgeRealloc(Hits, NewCap * sizeof(size_t)));
      }
      if (LLVM_UNLIKELY(!NewBuf)) {
        if (Hits != StackHits)
          bridgeFree(Hits);
        return nullptr;
      }
      Hits = NewBuf;
      HitCap = NewCap;
    }
    Hits[HitCount++] = static_cast<size_t>(P - S);
    P += DelimLen;
  }

  if (LLVM_UNLIKELY(HitCount >= UINT_MAX)) {
    if (Hits != StackHits)
      bridgeFree(Hits);
    return nullptr;
  }
  unsigned Parts = static_cast<unsigned>(HitCount) + 1;
  char **Arr = static_cast<char **>(
      bridgeAlloc(static_cast<uint64_t>(Parts) * sizeof(char *)));
  if (LLVM_UNLIKELY(!Arr)) {
    if (Hits != StackHits)
      bridgeFree(Hits);
    return nullptr;
  }

  // Build parts using recorded offsets -- no second strstr scan.
  size_t Prev = 0;
  unsigned Idx = 0;
  for (size_t I = 0; I < HitCount; ++I) {
    size_t Span = Hits[I] - Prev;
    char *Part = static_cast<char *>(bridgeAlloc(Span + 1));
    if (LLVM_UNLIKELY(!Part)) {
      for (unsigned J = 0; J < Idx; ++J)
        bridgeFree(Arr[J]);
      bridgeFree(Arr);
      if (Hits != StackHits)
        bridgeFree(Hits);
      return nullptr;
    }
    if (Span)
      std::memcpy(Part, S + Prev, Span);
    Part[Span] = '\0';
    Arr[Idx++] = Part;
    Prev = Hits[I] + DelimLen;
  }

  if (Hits != StackHits)
    bridgeFree(Hits);

  // Tail after the last delimiter.
  Arr[Idx] = bridgeStrDup(S + Prev);
  if (LLVM_UNLIKELY(!Arr[Idx])) {
    for (unsigned J = 0; J < Idx; ++J)
      bridgeFree(Arr[J]);
    bridgeFree(Arr);
    return nullptr;
  }
  *OutCount = Parts;
  return Arr;
}

// ===----------------------------------------------------------------------===
//  StrHash -- xxh3 64-bit hash (SIMD-accelerated when available)
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrHash(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  return llvm::xxh3_64bits(StringRef(S));
}

//  String search, replacement, case conversion, memory duplicate
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrFindStr(const char *Haystack, const char *Needle) {
  if (LLVM_UNLIKELY(!Haystack || !Needle))
    return UINT64_MAX;
  const char *P = std::strstr(Haystack, Needle);
  return P ? static_cast<uint64_t>(P - Haystack) : UINT64_MAX;
}

static void *bridgeMemDup(const void *Src, uint64_t Len) {
  if (LLVM_UNLIKELY(!Src || Len == 0))
    return nullptr;
  void *Dst = bridgeAlloc(Len);
  if (LLVM_LIKELY(Dst))
    std::memcpy(Dst, Src, static_cast<size_t>(Len));
  return Dst;
}

static char *bridgeStrReplace(const char *S, const char *Old,
                              const char *New) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Old || !New || Old[0] == '\0'))
    return bridgeStrDup(S);

  const char *Pos = std::strstr(S, Old);
  if (!Pos)
    return bridgeStrDup(S);

  size_t PrefixLen = static_cast<size_t>(Pos - S);
  size_t OldLen = std::strlen(Old);
  size_t NewLen = std::strlen(New);
  size_t SuffixLen = std::strlen(Pos + OldLen);
  if (LLVM_UNLIKELY(PrefixLen > SIZE_MAX - 1 ||
                    NewLen > SIZE_MAX - 1 - PrefixLen))
    return nullptr;
  size_t ResultLen = PrefixLen + NewLen;
  if (LLVM_UNLIKELY(ResultLen > SIZE_MAX - 1 ||
                    SuffixLen > SIZE_MAX - 1 - ResultLen))
    return nullptr;
  ResultLen += SuffixLen;

  char *Result = static_cast<char *>(bridgeAlloc(ResultLen + 1));
  if (LLVM_UNLIKELY(!Result))
    return nullptr;
  if (PrefixLen)
    std::memcpy(Result, S, PrefixLen);
  if (NewLen)
    std::memcpy(Result + PrefixLen, New, NewLen);
  if (SuffixLen)
    std::memcpy(Result + PrefixLen + NewLen, Pos + OldLen, SuffixLen);
  Result[ResultLen] = '\0';
  return Result;
}

static char *bridgeStrReplaceAll(const char *S, const char *Old,
                                 const char *New) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Old || !New || Old[0] == '\0'))
    return bridgeStrDup(S);

  size_t OldLen = std::strlen(Old);
  size_t NewLen = std::strlen(New);
  size_t SLen = std::strlen(S);

  // Single scan: collect hit offsets into a small stack-local buffer,
  // spilling to the heap only when there are many matches.
  size_t StackBuf[64];
  size_t *Hits = StackBuf;
  size_t HitCap = sizeof(StackBuf) / sizeof(StackBuf[0]);
  size_t HitCount = 0;

  const char *P = S;
  while ((P = std::strstr(P, Old)) != nullptr) {
    if (LLVM_UNLIKELY(HitCount == HitCap)) {
      if (HitCap > (SIZE_MAX / sizeof(size_t)) / 2) {
        if (Hits != StackBuf)
          bridgeFree(Hits);
        return nullptr;
      }
      size_t NewCap = HitCap * 2;
      size_t *NewBuf;
      if (Hits == StackBuf) {
        NewBuf = static_cast<size_t *>(
            bridgeAlloc(NewCap * sizeof(size_t)));
        if (LLVM_LIKELY(NewBuf))
          std::memcpy(NewBuf, StackBuf, HitCount * sizeof(size_t));
      } else {
        NewBuf = static_cast<size_t *>(
            bridgeRealloc(Hits, NewCap * sizeof(size_t)));
      }
      if (LLVM_UNLIKELY(!NewBuf)) {
        if (Hits != StackBuf)
          bridgeFree(Hits);
        return nullptr;
      }
      Hits = NewBuf;
      HitCap = NewCap;
    }
    Hits[HitCount++] = static_cast<size_t>(P - S);
    P += OldLen;
  }

  if (HitCount == 0) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return bridgeStrDup(S);
  }

  size_t Removed = HitCount * OldLen;
  size_t Added = 0;
  if (NewLen != 0) {
    if (LLVM_UNLIKELY(HitCount > SIZE_MAX / NewLen)) {
      if (Hits != StackBuf)
        bridgeFree(Hits);
      return nullptr;
    }
    Added = HitCount * NewLen;
  }
  size_t BaseLen = SLen - Removed;
  // Two-step guard: BaseLen could be 0 or large; SIZE_MAX-BaseLen-1
  // wraps around if BaseLen == SIZE_MAX (impossible in practice but
  // matches the unsigned-underflow-safe pattern used elsewhere).
  if (LLVM_UNLIKELY(BaseLen > SIZE_MAX - 1 ||
                    Added > SIZE_MAX - 1 - BaseLen)) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return nullptr;
  }
  size_t ResultLen = BaseLen + Added;

  char *Result = static_cast<char *>(bridgeAlloc(ResultLen + 1));
  if (LLVM_UNLIKELY(!Result)) {
    if (Hits != StackBuf)
      bridgeFree(Hits);
    return nullptr;
  }

  // Build the result in one pass using recorded offsets.
  char *Dst = Result;
  size_t Prev = 0;
  for (size_t I = 0; I < HitCount; ++I) {
    size_t Off = Hits[I];
    size_t Span = Off - Prev;
    if (Span)
      std::memcpy(Dst, S + Prev, Span);
    Dst += Span;
    if (NewLen)
      std::memcpy(Dst, New, NewLen);
    Dst += NewLen;
    Prev = Off + OldLen;
  }
  size_t Tail = SLen - Prev;
  std::memcpy(Dst, S + Prev, Tail);
  Dst[Tail] = '\0';

  if (Hits != StackBuf)
    bridgeFree(Hits);
  return Result;
}

static char *bridgeStrToLower(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *R = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!R))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    R[I] = static_cast<char>(asciiToLower(static_cast<unsigned char>(S[I])));
  R[Len] = '\0';
  return R;
}

static char *bridgeStrToUpper(const char *S) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Len = std::strlen(S);
  char *R = static_cast<char *>(bridgeAlloc(Len + 1));
  if (LLVM_UNLIKELY(!R))
    return nullptr;
  for (size_t I = 0; I < Len; ++I)
    R[I] = static_cast<char>(asciiToUpper(static_cast<unsigned char>(S[I])));
  R[Len] = '\0';
  return R;
}

// ===----------------------------------------------------------------------===
//  Bounded string duplication (strndup equivalent)
// ===----------------------------------------------------------------------===

static char *bridgeStrNDup(const char *S, uint64_t MaxLen) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  size_t Cap = (MaxLen > static_cast<uint64_t>(SIZE_MAX - 1))
                   ? (SIZE_MAX - 1)
                   : static_cast<size_t>(MaxLen);
  const void *Nul = Cap ? std::memchr(S, '\0', Cap) : nullptr;
  size_t CopyLen =
      Nul ? static_cast<size_t>(static_cast<const char *>(Nul) - S) : Cap;
  char *Buf = static_cast<char *>(bridgeAlloc(CopyLen + 1));
  if (LLVM_UNLIKELY(!Buf))
    return nullptr;
  if (CopyLen)
    std::memcpy(Buf, S, CopyLen);
  Buf[CopyLen] = '\0';
  return Buf;
}

// ===----------------------------------------------------------------------===
//  Character occurrence count
//
//  Normalize C to unsigned char before searching: std::strchr converts the
//  needle through (char), so values like 256, 512, ... silently collapse to
//  '\0' on 8-bit-char platforms and trigger an infinite loop on the null
//  terminator.  Reject the post-truncation zero, not just the literal 0.
// ===----------------------------------------------------------------------===

static uint64_t bridgeStrCountChar(const char *S, int C) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  unsigned char Needle = static_cast<unsigned char>(C);
  if (LLVM_UNLIKELY(Needle == 0))
    return 0;
  uint64_t Count = 0;
  for (const char *P = S; (P = std::strchr(P, Needle)) != nullptr; ++P)
    ++Count;
  return Count;
}

// ===----------------------------------------------------------------------===
//  Prefix-skip helper -- returns S+strlen(Prefix) when S starts with Prefix,
//  or nullptr otherwise.  Replaces the StrStartsWith + magic-offset idiom.
// ===----------------------------------------------------------------------===

static const char *bridgeStrAfterPrefix(const char *S, const char *Prefix) {
  if (LLVM_UNLIKELY(!S))
    return nullptr;
  if (LLVM_UNLIKELY(!Prefix || !*Prefix))
    return S;
  const char *P = S;
  const char *Q = Prefix;
  for (;; ++P, ++Q) {
    if (*Q == '\0')
      return P;
    if (*P != *Q)
      return nullptr;
  }
}

// ===----------------------------------------------------------------------===
//  Byte-needle search (memmem equivalent).  Returns offset, not pointer,
//  so the result survives Haystack relocations such as BinaryResize.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemFind(const void *Haystack, uint64_t HaystackLen,
                              const void *Needle, uint64_t NeedleLen) {
  if (LLVM_UNLIKELY(!Haystack || !Needle))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(NeedleLen == 0 || NeedleLen > HaystackLen))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return UINT64_MAX;

  const auto *H = static_cast<const unsigned char *>(Haystack);
  const auto *N = static_cast<const unsigned char *>(Needle);

  // Single-byte needle: defer to libc memchr (typically SIMD-accelerated).
  if (NeedleLen == 1) {
    const void *P = std::memchr(H, N[0], static_cast<size_t>(HaystackLen));
    if (!P)
      return UINT64_MAX;
    return static_cast<uint64_t>(static_cast<const unsigned char *>(P) - H);
  }

  // Two-Way / Boyer-Moore-Horspool would be faster for large inputs, but
  // pass authors typically scan short binary blobs, so a naive memchr-anchored
  // sweep wins on simplicity and stays branch-prediction-friendly.
  uint64_t Last = HaystackLen - NeedleLen;
  uint64_t I = 0;
  while (I <= Last) {
    const void *Hit = std::memchr(H + I, N[0],
                                  static_cast<size_t>(Last - I + 1));
    if (!Hit)
      return UINT64_MAX;
    uint64_t Off = static_cast<uint64_t>(
        static_cast<const unsigned char *>(Hit) - H);
    if (std::memcmp(H + Off, N, static_cast<size_t>(NeedleLen)) == 0)
      return Off;
    I = Off + 1;
  }
  return UINT64_MAX;
}

// ===----------------------------------------------------------------------===
//  Byte occurrence count -- delegates to libc memchr in a tight loop so the
//  hot path stays SIMD-accelerated on platforms where memchr is vectorized.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemCount(const void *Haystack, uint64_t HaystackLen,
                               int Byte) {
  if (LLVM_UNLIKELY(!Haystack || HaystackLen == 0))
    return 0;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return 0;
  unsigned char Needle = static_cast<unsigned char>(Byte);
  const auto *H = static_cast<const unsigned char *>(Haystack);
  size_t Remaining = static_cast<size_t>(HaystackLen);
  uint64_t Count = 0;
  while (Remaining > 0) {
    const void *Hit = std::memchr(H, Needle, Remaining);
    if (!Hit)
      break;
    ++Count;
    size_t Skip = static_cast<size_t>(
                      static_cast<const unsigned char *>(Hit) - H) +
                  1;
    H += Skip;
    Remaining -= Skip;
  }
  return Count;
}

// ===----------------------------------------------------------------------===
//  Single-byte search returning an offset.  Thin wrapper around memchr
//  that avoids the caller constructing a 1-byte needle array.
// ===----------------------------------------------------------------------===

static uint64_t bridgeMemFindByte(const void *Haystack, uint64_t HaystackLen,
                                  uint8_t Byte) {
  if (LLVM_UNLIKELY(!Haystack || HaystackLen == 0))
    return UINT64_MAX;
  if (LLVM_UNLIKELY(exceedsSizeT(HaystackLen)))
    return UINT64_MAX;
  const void *P =
      std::memchr(Haystack, Byte, static_cast<size_t>(HaystackLen));
  if (!P)
    return UINT64_MAX;
  return static_cast<uint64_t>(static_cast<const unsigned char *>(P) -
                               static_cast<const unsigned char *>(Haystack));
}

// ===----------------------------------------------------------------------===
//  Character class scanning (strspn / strcspn equivalents)
//
//  Build a 256-bit bitset from the class on entry then walk S with one
//  branch per byte.  Faster than the libc strspn/strcspn variants on
//  short character classes that revisit the class for every byte.
// ===----------------------------------------------------------------------===

namespace {
struct ByteClass {
  // 256 bits packed into 4x uint64_t.  Built once per call; queried by
  // ((Bits[B >> 6] >> (B & 63)) & 1).
  uint64_t Bits[4] = {0, 0, 0, 0};

  void add(unsigned char B) { Bits[B >> 6] |= (uint64_t{1} << (B & 63)); }
  bool contains(unsigned char B) const {
    return (Bits[B >> 6] >> (B & 63)) & uint64_t{1};
  }
  void buildFrom(const char *Class) {
    for (const unsigned char *P = reinterpret_cast<const unsigned char *>(
             Class);
         *P; ++P)
      add(*P);
  }
};
} // anonymous namespace

static uint64_t bridgeStrSpan(const char *S, const char *Accept) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  if (LLVM_UNLIKELY(!Accept || !*Accept))
    return 0;
  ByteClass C;
  C.buildFrom(Accept);
  const unsigned char *P = reinterpret_cast<const unsigned char *>(S);
  const unsigned char *Start = P;
  while (*P && C.contains(*P))
    ++P;
  return static_cast<uint64_t>(P - Start);
}

static uint64_t bridgeStrCSpn(const char *S, const char *Reject) {
  if (LLVM_UNLIKELY(!S))
    return 0;
  const unsigned char *P = reinterpret_cast<const unsigned char *>(S);
  const unsigned char *Start = P;
  if (LLVM_UNLIKELY(!Reject || !*Reject)) {
    while (*P)
      ++P;
    return static_cast<uint64_t>(P - Start);
  }
  ByteClass C;
  C.buildFrom(Reject);
  while (*P && !C.contains(*P))
    ++P;
  return static_cast<uint64_t>(P - Start);
}

// ===----------------------------------------------------------------------===
//  ASCII case-insensitive comparison
//
//  Lowercase only ASCII letters in [A-Z]; non-ASCII bytes pass through as
//  raw uint8.  Branchless lowercasing via subtract-shift-AND mask.
// ===----------------------------------------------------------------------===

// asciiToLower defined in BridgeCastHelpers.h

static int bridgeStrICompare(const char *A, const char *B) {
  if (LLVM_UNLIKELY(!A || !B))
    return (A == B) ? 0 : (A ? 1 : -1);
  const auto *PA = reinterpret_cast<const unsigned char *>(A);
  const auto *PB = reinterpret_cast<const unsigned char *>(B);
  for (;; ++PA, ++PB) {
    unsigned char CA = asciiToLower(*PA);
    unsigned char CB = asciiToLower(*PB);
    if (CA != CB)
      return (CA < CB) ? -1 : 1;
    if (CA == 0)
      return 0;
  }
}

int bridgeStrIEqual(const char *A, const char *B) {
  if (LLVM_UNLIKELY(!A || !B))
    return (A == B) ? 1 : 0;
  if (A == B)
    return 1;
  const auto *PA = reinterpret_cast<const unsigned char *>(A);
  const auto *PB = reinterpret_cast<const unsigned char *>(B);
  for (;; ++PA, ++PB) {
    unsigned char CA = asciiToLower(*PA);
    unsigned char CB = asciiToLower(*PB);
    if (CA != CB)
      return 0;
    if (CA == 0)
      return 1;
  }
}

// ===----------------------------------------------------------------------===
//  Path manipulation -- recognize both '/' and '\\' as separators in a
//  single pass.  Zero allocation; offsets are returned so the caller can
//  combine with StrSubstring/StrDup only when an owned copy is actually
//  needed.
// ===----------------------------------------------------------------------===

static uint64_t bridgePathBaseNameOffset(const char *Path) {
  if (LLVM_UNLIKELY(!Path))
    return 0;
  uint64_t LastSep = UINT64_MAX;
  for (const char *P = Path; *P; ++P) {
    if (*P == '/' || *P == '\\')
      LastSep = static_cast<uint64_t>(P - Path);
  }
  return LastSep == UINT64_MAX ? 0 : LastSep + 1;
}

static uint64_t bridgePathExtOffset(const char *Path) {
  if (LLVM_UNLIKELY(!Path))
    return UINT64_MAX;
  uint64_t BaseStart = 0;
  uint64_t LastDot = UINT64_MAX;
  for (const char *P = Path; *P; ++P) {
    char C = *P;
    if (C == '/' || C == '\\') {
      BaseStart = static_cast<uint64_t>(P - Path) + 1;
      LastDot = UINT64_MAX;
    } else if (C == '.') {
      LastDot = static_cast<uint64_t>(P - Path);
    }
  }
  // Reject "no dot", "trailing dot", and leading-dot dotfiles
  // ("/.bashrc" -> LastDot == BaseStart -> no extension).
  if (LastDot == UINT64_MAX || LastDot == BaseStart)
    return UINT64_MAX;
  if (Path[LastDot + 1] == '\0')
    return UINT64_MAX;
  return LastDot;
}

void populateStringBridge(NevercHostAPI &API) {
  API.StrLen = bridgeStrLen;
  API.StrDup = bridgeStrDup;
  API.StrConcat = bridgeStrConcat;
  API.StrEqual = bridgeStrEqual;
  API.IntToStr = bridgeIntToStr;
  API.UIntToStr = bridgeUIntToStr;
  API.StrFormat = bridgeStrFormat;
  API.StrFormatV = bridgeStrFormatV;

  API.MemCopy = bridgeMemCopy;
  API.MemSet = bridgeMemSet;
  API.MemMove = bridgeMemMove;
  API.MemCompare = bridgeMemCompare;
  API.MemReverse = bridgeMemReverse;
  API.MemZero = bridgeMemZero;
  API.MemDup = bridgeMemDup;

  API.DiagNoteF = bridgeDiagNoteF;
  API.DiagWarningF = bridgeDiagWarningF;
  API.DiagErrorF = bridgeDiagErrorF;
  API.DiagNoteV = bridgeDiagNoteV;
  API.DiagWarningV = bridgeDiagWarningV;
  API.DiagErrorV = bridgeDiagErrorV;

  API.AllocZeroed = bridgeAllocZeroed;

  API.StrStartsWith = bridgeStrStartsWith;
  API.StrEndsWith = bridgeStrEndsWith;
  API.StrContains = bridgeStrContains;
  API.StrCompare = bridgeStrCompare;
  API.StrToInt64 = bridgeStrToInt64;
  API.StrToUInt64 = bridgeStrToUInt64;
  API.StrSubstring = bridgeStrSubstring;
  API.StrTrim = bridgeStrTrim;

  API.StrFindChar = bridgeStrFindChar;
  API.StrFindLastChar = bridgeStrFindLastChar;
  API.ReallocArray = bridgeReallocArray;

  API.StrJoin = bridgeStrJoin;
  API.StrSplit = bridgeStrSplit;
  API.StrHash = bridgeStrHash;

  API.StrFindStr = bridgeStrFindStr;
  API.StrReplace = bridgeStrReplace;
  API.StrReplaceAll = bridgeStrReplaceAll;
  API.StrToLower = bridgeStrToLower;
  API.StrToUpper = bridgeStrToUpper;
  API.StrNDup = bridgeStrNDup;
  API.StrCountChar = bridgeStrCountChar;
  API.StrAfterPrefix = bridgeStrAfterPrefix;
  API.MemFind = bridgeMemFind;
  API.MemCount = bridgeMemCount;
  API.MemFindByte = bridgeMemFindByte;
  API.StrSpan = bridgeStrSpan;
  API.StrCSpn = bridgeStrCSpn;
  API.StrIEqual = bridgeStrIEqual;
  API.StrICompare = bridgeStrICompare;

  API.PathBaseNameOffset = bridgePathBaseNameOffset;
  API.PathExtOffset = bridgePathExtOffset;
}

} // namespace plugin
} // namespace neverc
