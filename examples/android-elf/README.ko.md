**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android ELF 예제

NeverC를 사용하여 Android용으로 크로스 컴파일한 ARM64 네이티브 ELF 바이너리입니다. 루팅된 Android 기기에서 `adb shell`을 통해 직접 실행할 수 있습니다. macOS, Windows, Linux에서 빌드 가능 — Android NDK나 CMake가 필요 없습니다.

NeverC는 `runtime/android/`에 Android sysroot(NDK r26c, API 21+)를 내장하고 있어, 한 번의 호출로 전처리, 컴파일, 최적화(자동 LTO), 링킹을 완료합니다.

## 빌드

저장소에서:

```bash
cd examples/android-elf
neverc make
```

독립형 NeverC 릴리스 사용:

```bash
neverc make NEVERC=/path/to/neverc
```

## 수동 빌드 (Make 없이)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## 배포 및 실행

adb로 기기에 전송하고 실행:

```bash
neverc make run
```

또는 수동으로:

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## 기능

- 기기 정보(`uname`)와 커널 버전 출력
- root/권한 상태 확인(`uid`/`euid`, `su` 경로)
- `liblog.so`를 동적 로드하여 `__android_log_print` 호출
- `/proc/self/maps`를 읽어 메모리 레이아웃 표시
- Android에서의 `dlopen`/`dlsym`, `readlink`, `fopen` 데모
