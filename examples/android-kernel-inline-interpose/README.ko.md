**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 함수 훅

`neverc_krt_interpose_register`를 사용하여 `do_faccessat` 진입점을 훅합니다. 데모 내용:

- **자동 체인**: 동일 대상에 여러 핸들러를 우선순위 순으로 실행
- **원본 호출 패턴**: 핸들러가 `orig` 포인터를 받아 원본 함수를 호출 가능
- **우선순위 제어**: 값이 작을수록 먼저 실행. 음수 값으로 다른 훅보다 먼저 실행
- **공존**: 대상이 이미 다른 모듈에 의해 훅되어 있어도 동작

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

핸들러 시그니처:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## 빌드

```bash
cd examples/android-kernel-inline-interpose
neverc make
```

`KERNEL`을 `515`, `601`, `606`, `612`로 변경하여 다른 커널 버전에 대응.

## 배포 및 실행

```bash
neverc make run
```

또는 수동:

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
```

## 언로드

```bash
neverc make rmmod
```
