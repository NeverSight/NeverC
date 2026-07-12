**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 내장 런타임 시스템](../README.ko.md)

# 컴파일 타임 문자열 해시 (`strhash`)

## 개요

NeverC는 순수 C를 위한 컴파일 타임/런타임 문자열 해시를 제공합니다. API 이름·명령 토큰 등을 정수 해시 비교로 빠르게 분기할 때 쓰이며, 바이너리에 평문 대조표를 남길 필요가 없습니다.

- **레이어 1 — 명시적 컴파일 타임 매크로**: `NC_STRHASH("string")` / `NEVERC_STRHASH("string")`는 Sema에서 정수 상수로 접힘
- **레이어 2 — 런타임 + 선택적 IR 접힘**: `neverc_strhash_rt` / `NC_STRHASH_AUTO`. `-fstrhash-fold`로 문자열 리터럴 인수의 런타임 호출을 상수화

두 레이어는 `-fstrhash-algo`로 선택한 동일 알고리즘(기본: FNV-1a 64-bit)을 공유해 컴파일 타임과 런타임 해시가 항상 일치합니다.

---

## 빠른 시작

### 레이어 1: 컴파일 타임 매크로

```c
#include <neverc/strhash/strhash.h>

static const uint64_t kApi = NC_STRHASH("NtQuerySystemInformation");

int is_api(const char *name) {
    return neverc_strhash_rt(name, strlen(name)) == kApi;
}
```

### 레이어 2: 자동 디스패치 + 접힘

```c
#include <neverc/strhash/strhash.h>
uint64_t h = NC_STRHASH_AUTO(name);
```

```bash
neverc -fstrhash-fold -fstrhash-algo=fnv64a main.c -o main
```

---

## 레이어 1: `NC_STRHASH` / `NEVERC_STRHASH`

모든 문자열 리터럴 유형(일반, UTF-8, 와이드, UTF-16, UTF-32) 지원. 비 리터럴 인수는 컴파일 오류. 변수는 `NC_STRHASH_AUTO` 또는 `neverc_strhash_rt` 사용.

### 알고리즘

| 플래그 값 | 설명 | 기본 |
|-----------|------|------|
| `fnv32a` | FNV-1a 32-bit | |
| `fnv64a` | FNV-1a 64-bit | **예** |
| `xxhash64` | XXHash64(seed 0) | |

---

## 레이어 2: `-fstrhash-fold`

`neverc_fnv_sum32a` / `neverc_fnv_sum64a` / `neverc_xxhash64`에 대한 상수 문자열 인수 호출을 정수 상수로 접습니다.

| 플래그 | 설명 | 기본 |
|--------|------|------|
| `-fstrhash-fold` | IR 접힘 활성화 | 꺼짐 |
| `-fno-strhash-fold` | 비활성화 | — |
| `-fstrhash-algo=<algo>` | 알고리즘 선택 | `fnv64a` |

---

## 사용자 정의 런타임 해시

```c
#define NC_STRHASH_HASH_FN(data, len) my_hash(data, len)
#include <neverc/strhash/strhash.h>
```

런타임 경로만 재정의. `NC_STRHASH()`는 계속 builtin / `-fstrhash-algo` 사용.

---

## 컴파일러 플래그 참고

| 플래그 | 설명 |
|--------|------|
| `-fstrhash-algo=fnv32a` | FNV-1a 32-bit 사용 |
| `-fstrhash-algo=fnv64a` | FNV-1a 64-bit 사용(기본) |
| `-fstrhash-algo=xxhash64` | XXHash64(seed 0) 사용 |
| `-fstrhash-fold` | 상수 문자열 인수의 런타임 해시 호출 접기 |
| `-fno-strhash-fold` | IR 접힘 비활성화 |
