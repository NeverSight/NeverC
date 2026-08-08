**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 Hello

최소한의 NeverC Android 커널 모듈(.ko). kprobe를 통해 `kallsyms_lookup_name`을 부트스트랩하고 로드 메시지를 출력한 후 정상 종료합니다. 컴파일→링크→insmod 전체 흐름 검증.

## 빌드

```bash
cd examples/android-kernel-hello
neverc make
```

다른 커널 버전은 `KERNEL`을 `515`, `601`, `606`, `612`로 변경하세요.

## 배포 및 실행

```bash
neverc make run
```

또는 수동으로:

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep neverc_krt_hello'
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
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
```

로드 순간의 새 로그가 터미널 1에 나타납니다. Ctrl+C로 중지합니다.

참고: 일부 Android 빌드는 `dmesg -w`를 지원하지 않습니다. `/proc/kmsg`는 root가 필요하지만 모듈 로드 디버깅에 더 안정적입니다.

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod neverc_krt_hello'
```
