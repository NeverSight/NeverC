**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 Syscall Interpose

`sys_call_table`의 포인터를 교체하여 `openat`을 훅합니다. ARM64 GKI 커널에서 `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`를 사용한 클래식 syscall 인터셉션을 시연합니다.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## 빌드

```bash
cd examples/android-kernel-syscall-interpose
neverc make
```

`KERNEL`을 `515`, `601`, `606`, `612`로 변경하여 다른 커널 버전에 대응.

## 배포 및 실행

```bash
neverc make run
```

또는 수동:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
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
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
```

로드 순간의 새 로그가 터미널 1에 나타납니다. Ctrl+C로 중지합니다.

참고: 일부 Android 빌드는 `dmesg -w`를 지원하지 않습니다. `/proc/kmsg`는 root가 필요하지만 모듈 로드 디버깅에 더 안정적입니다.

## 언로드

```bash
neverc make rmmod
```
