**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 커널 인라인 훅

`do_faccessat`에 대한 인라인 훅. 기본: 트램폴린을 이용한 단순 교체. `-DNVK_CONTEXT_HOOK` 사용 시: 전체 `nvk_reg_ctx` 레지스터 상태를 받는 컨텍스트 훅. BTI/PAC 안전 패칭, PC 상대 재배치, D-cache→I-cache 코히어런트 트램폴린 시연.

## 훅 모드

| | Simple Hook (기본) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **함수 시그니처** | 정확한 typedef 선언 필수 | 불필요 — `ctx->regs[0..7]`로 접근 |
| **재진입 가드** | 수동 (`nvk_hook_enter`/`leave`) | 내장 (`guard_task`) |
| **활성화/비활성화** | 수동 (`WRITE_ONCE`) | stub 내장 빠른 검사 |
| **원본 함수 호출** | `orig` 함수 포인터 사용 | 자동 (핸들러 후 실행) |
| **원본 함수 건너뛰기** | `orig` 미호출 | `NVK_CTX_SKIP(ctx, ret)` |
| **리디렉션** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **인자 수정** | `orig` 호출 전 파라미터 변경 | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP 안전성** | 호출자 저장 규약 | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **오버헤드** | 낮음 (4개 명령어 patch + trampoline) | 높음 (116개 명령어 stub + 전체 레지스터 저장) |
| **적합한 경우** | 알려진 시그니처, 성능 중요 | 모니터링, 불안정한 ABI, 빠른 프로토타이핑 |

**권장**: 반환 값 인터셉트나 엄격한 성능 요구가 없다면 context hook을 우선 사용하세요.

## 빌드

```bash
cd examples/android-kernel-inline-hook
neverc make
```

다른 커널 버전은 `KERNEL`을 `515`, `601`, `606`, `612`로 변경하세요.

## 배포 및 실행

```bash
neverc make run
```

또는 수동으로:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
