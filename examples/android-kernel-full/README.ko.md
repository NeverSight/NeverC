**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 커널 전체 SDK 데모

전체 SDK 통합 — 모든 NVK 서브시스템을 초기화하고 netlink 명령 인터페이스로 노출. 프로덕션 모듈의 참조 구현. interpose 엔진, 자격 증명 래퍼, 모듈 가시성, SELinux 정책 제어, 프로세스 열거, VMA 검사, 파일 I/O, 환경 감지, 통계를 포함.

## 빌드

```bash
cd examples/android-kernel-full
neverc make
```

다른 커널 버전은 `KERNEL`을 `515`, `601`, `606`, `612`로 변경하세요.

## 배포 및 실행

```bash
neverc make run
```

또는 수동으로:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod nvk_full'
```
