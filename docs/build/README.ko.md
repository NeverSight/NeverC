**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# `neverc build` / `neverc make`

NeverC는 **GNU Make 호환** 내장 드라이버를 제공합니다. `neverc build`와
`neverc make`는 같은 명령으로 Makefile을 읽고 변수·함수를 펼친 뒤 레시피를 실행합니다.
[`examples/`](../examples/README.ko.md)는 이 흐름용입니다.

**`neverc.toml` 프로젝트 도구가 아닙니다.** 일반 Make 옵션과 `VAR=value`를 쓰세요.

## 구문

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

옵션 목록은 `neverc make --help`.

## 옵션

| 옵션 | 의미 |
|------|------|
| `-f FILE` | 지정 Makefile 사용 |
| `-j [N]` | 병렬 작업(`-j`만 쓰면 CPU 수) |
| `-C DIR` | Makefile 읽기 전 디렉터리 변경 |
| `-n`, `--dry-run` | 실행 없이 출력 |
| `-k`, `--keep-going` | 오류 후에도 계속 |
| `-s`, `--silent` | 레시피 에코 안 함 |
| `-B`, `--always-make` | 무조건 재빌드 |
| `-p` | 규칙/변수 DB 출력 |
| `VAR=VALUE` | 명령줄 변수 |
| `-h`, `--help` | 사용법 |

## Makefile 탐색 순서

`-f` 생략 시: `GNUmakefile` → `makefile` → `Makefile`.

## 지원 Make 표면(요약)

규칙/패턴 규칙, `.PHONY`, 레시피 접두사, 대입, 조건, `include`/`export`,
`subst`/`patsubst`/`wildcard`/`foreach`/`call`/`eval`/`shell` 등.
`MAKE_VERSION`은 호환용으로 `4.3`. 의도적으로 좁힌 부분집합이며 완전한 GNU Make가 아닙니다.

## 전형적인 Makefile

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

교차 컴파일 예제는 종종 `ARCH=…`/`TARGET=…`를 넘깁니다. 자세한 내용은
[예제](../examples/README.ko.md).

## 관련 명령

| 명령 | 용도 |
|------|------|
| `neverc file.c -o out` | Makefile 없는 단일 컴파일 |
| [`neverc run`](../run/README.ko.md) | 호스트 임시 컴파일·실행 |
| [`neverc runtime`](../runtime/README.ko.md) | 교차용 sysroot 설치 |
| [릴리스와 `--strip`](../release-builds/README.ko.md) | 배포용 최종 이미지 strip |
