**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md)

# 로컬 개발

소스에서 NeverC를 빌드하고 로컬 개발 환경을 설정하는 가이드입니다.

---

## 사전 요구 사항

- CMake 3.20+
- Ninja
- C++17 호스트 컴파일러 (GCC, Clang 또는 MSVC)

---

## 빌드

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
cmake --build build-neverc --target neverc
```

`ccache` / `sccache`가 감지되면 자동으로 활성화됩니다.

### 테스트 포함 빌드

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake -DNEVERC_INCLUDE_TESTS=ON
cmake --build build-neverc --target check-neverc
```

---

## PATH 설정 (macOS / Linux)

빌드 후 `neverc` 바이너리는 `build-neverc/bin/neverc`에 위치합니다. 헬퍼 스크립트를 사용하여 `PATH`에 추가하면 매번 전체 경로를 입력할 필요가 없습니다:

```bash
source ./tools/neverc-env.sh
```

이제 `neverc`를 직접 실행할 수 있습니다:

```bash
neverc --version
neverc -c hello.c -o hello.o
```

### PATH에서 제거

로컬 빌드를 `PATH`에서 제거하려면 같은 셸 세션에서 다음을 실행합니다:

```bash
source ./tools/neverc-env.sh --remove   # 또는 -r
```

### 영구 설정

`source` 행을 셸 rc 파일(`~/.zshrc`, `~/.bashrc` 또는 `~/.profile`)에 자동 추가합니다:

```bash
source ./tools/neverc-env.sh --install
```

실행 취소:

```bash
source ./tools/neverc-env.sh --uninstall
```

---

## Windows (CMD)

Windows에서는 `.bat` 스크립트를 사용합니다 (관리자 권한 불필요):

```cmd
tools\neverc-env.bat             &REM PATH에 추가 (현재 세션)
tools\neverc-env.bat --remove    &REM PATH에서 제거 (현재 세션)
tools\neverc-env.bat --global    &REM setx로 사용자 PATH에 영구 추가
tools\neverc-env.bat --global -r &REM setx로 사용자 PATH에서 영구 제거
```

Unix 스크립트와 달리 `source`가 필요 없습니다 — `.bat`는 현재 `cmd` 세션을 직접 수정합니다. `--global`은 `setx`를 사용하여 사용자 수준 레지스트리에 기록합니다 (관리자 권한 불필요).

---

## macOS 사전 빌드 바이너리

릴리스는 Apple Developer ID 인증서로 서명되고 Apple에 의해 공증되었습니다. 아카이브를 추출하여 바로 사용할 수 있습니다.

---

## Windows로 크로스 컴파일

NeverC는 `runtime/`에 각 플랫폼 SDK(Windows SDK/WDK, Linux sysroot, macOS sysroot, Android NDK)를 번들로 포함하고 있어 외부 SDK 설정이 필요 없습니다.

```bash
neverc --target=x86_64-pc-windows-msvc \
  -fbuiltin-string -o hello.exe hello.c -lkernel32
```

Windows shellcode(`-fshellcode`, PEB 임포트 해결 등)에 대해서는 [shellcode 컴파일러 문서](../shellcode-compiler/README.ko.md)를 참조하세요.

---

## 확인

```bash
neverc --version
echo 'int main(void) { return 0; }' > /tmp/hello.c
neverc -c /tmp/hello.c -o /tmp/hello.o
```
