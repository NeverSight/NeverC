**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 커널 Syscall 훅

`openat`에 대한 시스템 콜 테이블 교체. 기본: 테이블 항목 교환. `-DNVK_SYSCALL_INLINE_HOOK` 사용 시: 핸들러 함수 프롤로그를 패칭. `nvk_syscall_replace`/`nvk_syscall_restore` 및 arm64 시스템 콜 번호 정의 시연.

## 빌드

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

다른 커널 버전은 `KERNEL`을 `515`, `601`, `606`, `612`로 변경하세요.

## 배포 및 실행

```bash
neverc make run
```

또는 수동으로:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
