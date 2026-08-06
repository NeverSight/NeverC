**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 저가시성

모듈 가시성 관리 데모. 컴파일 플래그: 없음=기본 리스트 가시성, `-DNVK_LOWVIS_FILTER`=전체 가시성 필터(리스트+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=확장(dmesg+PID+마운트+maps), `-DNVK_LOWVIS_CRED`=자격 증명 래퍼 데모(`struct cred`), `-DNVK_LOWVIS_SELINUX`=SELinux 강제 상태 데모(permissive).

## 빌드

```bash
cd examples/android-kernel-lowvis
neverc make
```

다른 커널 버전은 `KERNEL`을 `515`, `601`, `606`, `612`로 변경하세요.

## 배포 및 실행

```bash
neverc make run
```

또는 수동으로:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
```

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
