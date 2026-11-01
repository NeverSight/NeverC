/*===---- hleinterlockedintrin.h - Native Microsoft HLE aliases -----------===
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *===----------------------------------------------------------------------===*/
#ifndef __HLEINTERLOCKEDINTRIN_H
#define __HLEINTERLOCKEDINTRIN_H
#include <hlecontextintrin.h>

/* The caller owns the complete HLE critical region, as with the original
 * Microsoft interfaces. No forced-abort boundary is inserted here. */
#define __HLE_FN __attribute__((__always_inline__, __nodebug__))
static __inline__ long __HLE_FN
_InterlockedExchange_HLEAcquire(long volatile *_Target, long _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XCHG, sizeof(long),
               __HLE_ACQUIRE);
  return (long)_State.RCX;
}
static __inline__ long __HLE_FN
_InterlockedExchange_HLERelease(long volatile *_Target, long _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XCHG, sizeof(long),
               __HLE_RELEASE);
  return (long)_State.RCX;
}
static __inline__ __int64 __HLE_FN
_InterlockedExchange64_HLEAcquire(__int64 volatile *_Target, __int64 _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XCHG, sizeof(__int64),
               __HLE_ACQUIRE);
  return (__int64)_State.RCX;
}
static __inline__ __int64 __HLE_FN
_InterlockedExchange64_HLERelease(__int64 volatile *_Target, __int64 _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XCHG, sizeof(__int64),
               __HLE_RELEASE);
  return (__int64)_State.RCX;
}
static __inline__ long __HLE_FN
_InterlockedExchangeAdd_HLEAcquire(long volatile *_Target, long _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XADD, sizeof(long),
               __HLE_ACQUIRE);
  return (long)_State.RCX;
}
static __inline__ long __HLE_FN
_InterlockedExchangeAdd_HLERelease(long volatile *_Target, long _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XADD, sizeof(long),
               __HLE_RELEASE);
  return (long)_State.RCX;
}
static __inline__ __int64 __HLE_FN _InterlockedExchangeAdd64_HLEAcquire(
    __int64 volatile *_Target, __int64 _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XADD, sizeof(__int64),
               __HLE_ACQUIRE);
  return (__int64)_State.RCX;
}
static __inline__ __int64 __HLE_FN _InterlockedExchangeAdd64_HLERelease(
    __int64 volatile *_Target, __int64 _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_XADD, sizeof(__int64),
               __HLE_RELEASE);
  return (__int64)_State.RCX;
}
static __inline__ long __HLE_FN _InterlockedCompareExchange_HLEAcquire(
    long volatile *_Target, long _Value, long _Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(long),
               __HLE_ACQUIRE);
  return (long)_State.RAX;
}
static __inline__ long __HLE_FN _InterlockedCompareExchange_HLERelease(
    long volatile *_Target, long _Value, long _Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(long),
               __HLE_RELEASE);
  return (long)_State.RAX;
}
static __inline__ __int64 __HLE_FN _InterlockedCompareExchange64_HLEAcquire(
    __int64 volatile *_Target, __int64 _Value, __int64 _Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(__int64),
               __HLE_ACQUIRE);
  return (__int64)_State.RAX;
}
static __inline__ __int64 __HLE_FN _InterlockedCompareExchange64_HLERelease(
    __int64 volatile *_Target, __int64 _Value, __int64 _Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(__int64),
               __HLE_RELEASE);
  return (__int64)_State.RAX;
}
static __inline__ void *__HLE_FN _InterlockedCompareExchangePointer_HLEAcquire(
    void *volatile *_Target, void *_Value, void *_Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(void *),
               __HLE_ACQUIRE);
  return (void *)_State.RAX;
}
static __inline__ void *__HLE_FN _InterlockedCompareExchangePointer_HLERelease(
    void *volatile *_Target, void *_Value, void *_Comparand) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value,
                                 .RAX = (unsigned long long)_Comparand};
  __hle_native((void *)_Target, &_State, __HLE_CMPXCHG, sizeof(void *),
               __HLE_RELEASE);
  return (void *)_State.RAX;
}
static __inline__ void __HLE_FN _Store_HLERelease(long volatile *_Target,
                                                  long _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_MOV, sizeof(long),
               __HLE_RELEASE);
}
static __inline__ void __HLE_FN _Store64_HLERelease(__int64 volatile *_Target,
                                                    __int64 _Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_MOV, sizeof(__int64),
               __HLE_RELEASE);
}
static __inline__ void __HLE_FN
_StorePointer_HLERelease(void *volatile *_Target, void *_Value) {
  struct __hle_context _State = {.RCX = (unsigned long long)_Value};
  __hle_native((void *)_Target, &_State, __HLE_MOV, sizeof(void *),
               __HLE_RELEASE);
}

#undef __HLE_FN
#endif
