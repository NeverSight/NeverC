**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 Probe

`neverc_krt_probe_register`를 사용하여 `do_faccessat` 내부의 임의 명령어(진입점 아님)를 훅합니다. 데모 내용:

- **임의 주소 훅**: 함수 진입점뿐 아니라 모든 명령어를 훅 가능
- **전체 레지스터 컨텍스트**: `neverc_krt_reg_ctx`로 모든 GPR 읽기/쓰기
- **자동 체인**: 동일 주소에 여러 핸들러를 우선순위 순으로 실행
- **제어 흐름**: `NEVERC_KRT_CTX_SKIP`으로 중단, `NEVERC_KRT_CTX_REDIRECT`로 리다이렉트

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

핸들러 시그니처:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## 빌드

```bash
cd examples/android-kernel-probe
neverc make
```

`KERNEL`을 `515`, `601`, `606`, `612`로 변경하여 다른 커널 버전에 대응.

## 배포 및 실행

```bash
neverc make run
```

또는 수동:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## 언로드

```bash
neverc make rmmod
```
