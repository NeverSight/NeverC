**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# `neverc run`

C 또는 NeverC 프로그램을 **임시 실행 파일**로 컴파일하고 **로컬 호스트**에서 실행한 뒤 종료 상태를 반환하고 아티팩트를 삭제합니다. 워크플로는 의도적으로 `go run`과 유사합니다.

바이너리를 보관하거나 배포하거나 디버거로 디버깅하려면 일반 컴파일 호출(`neverc ... -o output`)을 사용하세요.

## 구문

```text
neverc run [컴파일러 플래그] file.c [file2.nc ...] [프로그램 인자...]
neverc run [컴파일러 인자...] -- [프로그램 인자...]
```

`neverc run --help`로 내장 요약도 볼 수 있습니다.

## 인자 파싱

`neverc run`은 다음 두 규칙 중 하나로 인자를 **컴파일러 호출**과 선택적 **프로그램 인자**로 나눕니다.

### 기본(Go 스타일) 분리

1. 왼쪽에서 오른쪽으로 스캔해 `.c` 또는 `.nc`로 끝나고 `-`로 시작하지 않는 첫 인자를 찾습니다.
2. **첫 소스 이전과 연속 `.c`/`.nc` 소스**는 모두 컴파일러로 전달합니다.
3. 연속 소스 **이후** 인자는 임시 프로그램의 `argv`로 전달합니다.

예:

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

참고:

- run 소스로 취급되는 확장자는 `.c`와 `.nc`뿐입니다. `-DGENERATED=.c`처럼 `-`로 시작하는 플래그는 컴파일러 쪽에 남습니다.
- 여러 소스는 일반 다중 파일 링크처럼 하나의 임시 바이너리로 컴파일됩니다.

### 명시적 `--` 구분

소스 목록 **뒤**에 컴파일러 인자(링커 플래그, 비소스 입력, `-x c -` 등)가 필요하면 `--`로 컴파일러 꼬리와 프로그램 인자를 구분합니다:

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

`--` 앞은 `neverc`로 그대로 전달(내부 `-o <temp>` 추가)되고, `--` 뒤는 프로그램 인자가 됩니다.

## 실행 시 동작

| 항목 | 동작 |
|------|------|
| 작업 디렉터리 | 임시 프로그램은 **현재 디렉터리**에서 실행. 상대 경로는 일반 바이너리와 동일 |
| 환경 | 현재 환경 상속(`PATH`, export된 변수 등) |
| 표준 I/O | stdin/stdout/stderr가 임시 프로세스에 연결. 파이프와 리다이렉트도 정상 동작 |
| 종료 상태 | 성공 시 **프로그램** 종료 코드. 컴파일 실패 시 **컴파일러** 종료 코드를 반환하고 프로그램은 실행하지 않음 |
| 임시 파일 | 실행 파일은 고유한 `neverc-run-*` 디렉터리에 있으며 실행 후 삭제된다(성공·실패 무관). 정리 실패는 별도로 보고된다. |

## 예제

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## 제한 및 주의

- **호스트 실행만.** 크로스 컴파일 플래그(`-target ...`)로 컴파일할 수 있어도 임시 바이너리는 항상 호출한 머신에서 실행됩니다.
- **영구 아티팩트 없음.** 완료 후 바이너리가 삭제됩니다. 디버거가 필요하면 `neverc ... -o out`을 사용하세요.
- **동일 `neverc` 툴체인.** `run`을 처리한 `neverc` 바이너리를 다시 호출하며 컴파일러 플래그를(내부 `-o` 제외) 그대로 전달합니다.
- **`.nc` 소스.** `.c`와 같은 규칙. `.nc`용 언어 확장은 자동으로 적용됩니다.

## 관련 명령

| 명령 | 용도 |
|------|------|
| `neverc file.c -o out` | 바이너리 보관, 크로스 컴파일, 빌드 스크립트 통합 |
| [`neverc build` / `neverc make`](../build/README.ko.md) | Makefile 기반 GNU Make 호환 빌드 |
| `neverc run --help` | 내장 사용법 요약 |
