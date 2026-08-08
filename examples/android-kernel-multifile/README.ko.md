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

## 커널 로그(실시간)

기기에서 `cat /proc/kmsg`를 실행하면 커널 ring buffer를 실시간으로 볼 수 있습니다. Windows **DbgView**와 비슷합니다. `insmod`가 모호한 오류만 돌려주거나 vermagic, modversions, section 크기 등 실제 거부 이유를 확인할 때 사용하세요.

터미널 1(계속 실행):

```bash
adb shell
su
cat /proc/kmsg
```

터미널 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

로드 순간의 새 로그가 터미널 1에 나타납니다. Ctrl+C로 중지합니다.

참고: 일부 Android 빌드는 `dmesg -w`를 지원하지 않습니다. `/proc/kmsg`는 root가 필요하지만 모듈 로드 디버깅에 더 안정적입니다.

## 언로드

```bash
neverc make rmmod
```
