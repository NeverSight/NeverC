**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# Android 커널 저가시성

모듈 가시성 관리 데모. 컴파일 플래그: 없음=기본 리스트 가시성, `-DNVK_LOWVIS_FILTER`=전체 가시성 필터(리스트+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=확장(dmesg+PID+마운트+maps), `-DNVK_LOWVIS_CRED`=자격 증명 래퍼 데모(`struct cred`), `-DNVK_LOWVIS_SELINUX`=SELinux 강제 상태 데모(permissive).

## 빌드

```bash
cd examples/android-kernel-lowvis
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 전환
```

다른 커널 프리셋은 예를 들어 `neverc make KERNEL=612 release`로 선택합니다.
`neverc make release`는 `-O2 --strip`을 선택합니다. Makefile은 선택한
`KERNEL`과 `PROFILE`을 `.nvk-build-flags`에 기록하므로 이후 `make push`,
`make run`, 대상 없는 `make`가 같은 산출물을 사용합니다. 이 상태 파일이 없으면
`make`는 debug를 기본값으로 사용합니다. `make debug` 또는 명시적인
`PROFILE=...`는 저장된 프로필을 갱신하고, `make clean`은 상태 파일을 삭제하여
다음 빌드를 debug로 되돌립니다.

NeverC는 IDA에서 착안하되 예약 접두사를 쓰지 않는 릴리스 이름을 다섯 종류로
기록합니다. 함수는 `fn_HEX`, 실행 가능한 무형식 레이블은 `code_HEX`, 객체는
`obj_HEX`, 그 밖의 무형식 레이블은 `sym_HEX`, 절대 심볼은 `abs_HEX`입니다.
일반 할당 정의에서 `HEX`는 최종 `SHF_ALLOC` 섹션 배치로부터 결정적으로 계산한
`analysis EA`입니다(`abs_HEX`는 대신 절대 `st_value`를 사용합니다). 이는 hash
(해시), encryption(암호화), file offset(파일 오프셋), ELF virtual address
(ELF 가상 주소), runtime kernel address(런타임 커널 주소)가 아닙니다. NeverC는
예약된 `sub_`/`loc_` 형식이나 의도적으로 빈 일반 이름도 저장하지 않습니다.

정확히 보존할 이름, IDA의 합성 `extern` 보기, 보안 경계, 확정 처리와 서명의 순서는
[릴리스 및 스트립 정책](../../docs/release-builds/README.ko.md)을 참조하십시오.

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
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
```

로드 순간의 새 로그가 터미널 1에 나타납니다. Ctrl+C로 중지합니다.

참고: 일부 Android 빌드는 `dmesg -w`를 지원하지 않습니다. `/proc/kmsg`는 root가 필요하지만 모듈 로드 디버깅에 더 안정적입니다.

## 언로드

```bash
neverc make rmmod
```

또는 수동으로:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
