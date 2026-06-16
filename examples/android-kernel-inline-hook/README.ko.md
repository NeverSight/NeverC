**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 커널 인라인 훅

`do_faccessat`에 대한 인라인 훅. 기본: 트램폴린을 이용한 단순 교체. `-DNVK_CONTEXT_HOOK` 사용 시: 전체 `nvk_reg_ctx` 레지스터 상태를 받는 컨텍스트 훅. BTI/PAC 안전 패칭, PC 상대 재배치, D-cache→I-cache 코히어런트 트램폴린 시연.

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
