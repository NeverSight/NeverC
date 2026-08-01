**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../docs/i18n/README.ko.md)

# NeverC 예제

NeverC의 크로스 플랫폼 컴파일 기능을 보여주는 빌드 가능한 예제. 모두 macOS / Linux에서 크로스 컴파일 가능 — Windows 빌드 환경 불필요.

---

## 예제 목록

### 서버 백엔드

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [권위형 게임 서버](../../examples/network-authoritative-server/README.ko.md) | 크로스 플랫폼 게임 백엔드 | 고정 60 Hz tick, TCP 세션, UDP/QUIC 입력, 재생 방지 |
| [안티치트 수집기](../../examples/network-anticheat-collector/README.ko.md) | 강화된 텔레메트리 수집 | mTLS, 스트리밍 NRPC, HMAC 텔레메트리, 경계가 있는 감사 파이프라인 |

### Windows

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [Windows 커널 드라이버](../../examples/windows-driver/README.ko.md) | 최소 WDM 커널 드라이버 | **x64**(기본)와 **ARM64**용 `.sys` 크로스 컴파일, 자동 LTO, 내장 링커 |
| [Windows 드라이버 + CET](../../examples/windows-driver-cet/README.ko.md) | Intel CET 섀도 스택 커널 드라이버 | CET 호환 커널 코드(**x64 전용**), `/guard:ehcont` |
| [Windows 드라이버 + 부동 소수점](../../examples/windows-driver-float/README.ko.md) | 부동 소수점/SIMD 커널 드라이버 | **x64**와 **ARM64**에서 커널 모드 안전 부동 소수점 |
| [Windows Ring3 EXE](../../examples/windows-exe/README.ko.md) | 사용자 모드 콘솔 앱 | GetSystemInfo, 프로세스 열거, VirtualAlloc |
| [Windows Ring3 DLL](../../examples/windows-dll/README.ko.md) | 사용자 모드 DLL | ReadProcessMemory, VirtualAllocEx, 모듈 열거 |

### Linux

| 예제 | 설명 | 주요 기능 |
|------|------|--------|
| [Linux Hello World](../../examples/linux-hello/README.ko.md) | 최소한의 C 프로그램 | macOS/Windows에서 크로스 컴파일 |
| [Linux POSIX](../../examples/linux-posix/README.ko.md) | POSIX 시스템 프로그래밍 | pthreads, mmap, pipe, 시그널 |
| [Linux 정적](../../examples/linux-static/README.ko.md) | 완전 정적 바이너리 | `-static` 링크 |
| [Linux 네트워크](../../examples/linux-network/README.ko.md) | TCP 소켓 데모 | 클라이언트/서버 |
| [Linux 수학 + zlib](../../examples/linux-math/README.ko.md) | 수학 + 압축 | 삼각 함수, zlib, CRC32 |

### macOS

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [macOS 애플리케이션](../../examples/macos-app/README.ko.md) | 네이티브 Mach-O 실행 파일 | sysctl, uname, Mach host_info/task_info, 프로세스 조회 |
| [macOS 동적 라이브러리](../../examples/macos-dylib/README.ko.md) | 네이티브 `.dylib` 라이브러리 | Mach vm_read/vm_write, vm_alloc/vm_dealloc, task_info, XOR |

### Android

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [Android ELF](../../examples/android-elf/README.ko.md) | 루팅 기기용 네이티브 ARM64 바이너리 | Android 크로스 컴파일, dlopen/liblog, /proc 정보, root 확인 |
| [Android 공유 라이브러리](../../examples/android-so/README.ko.md) | 네이티브 ARM64 `.so` 라이브러리 | 공유 라이브러리, mmap RWX, XOR 암호화 |

### Android 커널 모듈 (.ko)

커널 소스 트리 불필요 — NeverC는 번들된 최소 런타임으로 컴파일합니다. 단일 소스로 GKI 5.10–6.12 지원.

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [커널 Hello](../../examples/android-kernel-hello/README.ko.md) | 최소 `.ko` 모듈 | kprobe 기반 kallsyms 부트스트랩, 최소 insmod 검증 |
| [커널 드라이버 템플릿](../../examples/android-kernel-driver/README.ko.md) | 동적 심볼 해석 템플릿 | `kallsyms_lookup_name`, GKI 안정 ABI, 5.10–6.12 |
| [커널 인라인 훅](../../examples/android-kernel-inline-interpose/README.ko.md) | `do_faccessat` 인라인 훅 | BTI/PAC 안전 패치, 컨텍스트 훅 모드, PC 상대 재배치 |
| [커널 Syscall 훅](../../examples/android-kernel-syscall-interpose/README.ko.md) | syscall 테이블 / inline / context interpose | `sys_call_table` 교체, 인라인 훅, 컨텍스트 훅 |
| [커널 저가시성](../../examples/android-kernel-lowvis/README.ko.md) | 모듈 가시성 관리 | list/sysfs/proc 가시성, 자격 증명 래퍼, SELinux 강제 상태 |
| [커널 Full SDK](../../examples/android-kernel-full/README.ko.md) | 완전 SDK 통합 | Netlink IPC, interpose, 자격 증명 래퍼, 모듈 가시성, SELinux 정책 제어, VMA, 파일 I/O |
| [커널 Chardev](../../examples/android-kernel-chardev/README.ko.md) | 문자 장치 + ioctl | `misc_register`, ioctl 디스패치, `/proc` seq_file |
| [커널 Netlink](../../examples/android-kernel-netlink/README.ko.md) | 양방향 netlink IPC | PING/VERSION/ECHO 명령, `nvk_nl_open`/`nvk_nl_reply` |
| [커널 Probe](../../examples/android-kernel-probe/README.ko.md) | 임의 명령어 프로브 | `neverc_krt_probe_register`, 전체 레지스터 컨텍스트, 우선순위 체이닝, 건너뛰기/리다이렉트 |
| [커널 다중 파일 모듈](../../examples/android-kernel-multifile/README.ko.md) | 다중 파일 커널 모듈 | `NEVERC_KRT_BOOTSTRAP()` 한 번만, `weak_odr` 공유 상태, init/interpose/helper 분리 |

---

## 빠른 시작

모든 예제는 동일한 패턴을 따릅니다:

```bash
cd examples/example-name
neverc make
```

필요하면 컴파일러 경로를 재정의합니다:

```bash
neverc make NEVERC=/path/to/neverc
```

Windows 드라이버 예제는 `ARCH`로 아키텍처를 선택합니다(기본값 x64). CET 예제는
x64 전용입니다 — CET은 x86 기능입니다:

```bash
neverc make ARCH=x64        # Build for x64 (default)
neverc make ARCH=arm64      # Build for ARM64
neverc make all-arch        # Build every architecture the example supports
neverc make TESTSIGN=1      # Attach an Authenticode test signature
```

Linux 예제는 아키텍처 선택을 지원합니다:

```bash
neverc make TARGET=aarch64-linux-gnu   # Build for ARM64
neverc make TARGET=x86_64-linux-gnu    # Build for x86_64 (default)
```

macOS 예제는 아키텍처 선택을 지원합니다:

```bash
neverc make TARGET=arm64-apple-macos     # Build for Apple Silicon (default)
neverc make TARGET=x86_64-apple-macos    # Build for Intel
```

Android 예제는 기본적으로 ARM64를 대상으로 합니다:

```bash
cd examples/android-elf
neverc make            # Build
neverc make run        # Build + push to device + run via adb
```

---

## 크로스 플랫폼 하이라이트

- **단일 툴체인**: NeverC는 전처리, 컴파일, 최적화(자동 LTO), 링크를 한 번의 호출로 처리합니다
- **번들 SDK**: Windows SDK/WDK, Linux sysroot(Ubuntu 22.04), macOS sysroot(macOS 14), Android sysroot(NDK r26c, API 21+)가 `runtime/`에 번들되어 있습니다 — 외부 의존성 제로
- **호스트 독립적**: macOS(arm64/x86_64), Linux(x86_64/aarch64), Windows에서 동일한 명령으로 빌드
- **멀티 타깃**: 임의의 호스트에서 Windows PE(`.sys`/`.exe`/`.dll`), Linux ELF, macOS Mach-O(`.dylib`), Android ELF로 크로스 컴파일
- **디버그 지원**: `-g`를 전달하면 DWARF 디버그 정보 포함; `llvm-dwarfdump`로 검사
