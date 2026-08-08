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
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 전환
```

다른 커널 프리셋은 예를 들어 `neverc make KERNEL=612 release`로 선택합니다.
Makefile은 `KERNEL`과 `PROFILE`을 모두 저장하므로 이후 `make push`/`run`이
다른 프로필로 조용히 되돌아가지 않습니다.

release 스트립은 NeverC에 내장되어 있으며 커널 모듈에 안전한 범위만
적용합니다. DWARF, `.comment`, 재배치에 필요하지 않은 private/undefined
심볼 이름은 제거하지만 ET_REL 심볼/문자열 테이블, 재배치, import, global
정의, `__versions`, `.codetag.alloc_tags`와 로더 ABI 데이터는 유지합니다.
strip-all이나 난독화가 아니므로 재배치에 필요한 이름은 남을 수 있습니다.
서명할 경우 먼저 스트립한 뒤 최종 바이트에 서명하세요. `clean`에서
스트립하거나 `.ko`에 `llvm-strip --strip-all`을 사용하거나
`.codetag.alloc_tags`/`__codetag_*` 섹션을 무작정 제거하면 안 됩니다.

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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

로드 순간의 새 로그가 터미널 1에 나타납니다. Ctrl+C로 중지합니다.

참고: 일부 Android 빌드는 `dmesg -w`를 지원하지 않습니다. `/proc/kmsg`는 root가 필요하지만 모듈 로드 디버깅에 더 안정적입니다.

## 언로드

```bash
neverc make rmmod
```
