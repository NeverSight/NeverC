**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 멀티파일 모듈

멀티파일 NeverC 커널 모듈 데모. 핵심:

- **단일 부트스트랩**: `NEVERC_KRT_BOOTSTRAP()`는 `module_init`에서 한 번만 호출
- **공유 상태**: 컴파일러가 모든 `neverc_krt_*` 상태를 `weak_odr` 링크로 승격하여 모든 `.c` 파일이 동일한 리졸버, 캐시, 서브시스템 상태를 공유
- **분할 아키텍처**: `main.c`(초기화/종료), `interposes.c`(훅 로직), `utils.c`(헬퍼)

## 빌드

```bash
cd examples/android-kernel-multifile
neverc make
```

`KERNEL`을 `515`, `601`, `606`, `612`로 변경하여 다른 커널 버전에 대응.

## 배포 및 실행

```bash
neverc make run
```

또는 수동:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## 언로드

```bash
neverc make rmmod
```
