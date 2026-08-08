**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# `neverc update`

**릴리스 설치**의 컴파일러와 이미 설치된 교차 컴파일 runtime을 **하나의 구체적 릴리스 태그**로
함께 맞춥니다. `neverc upgrade`는 동의어입니다.

`install.sh`(또는 `~/.neverc` 설치) 이후 업/다운그레이드용입니다. CMake/Ninja 소스 빌드
트리는 갱신하지 않습니다. PATH를 바꾸고 다시 빌드하세요. [로컬 개발](../local-dev/README.ko.md).

## 구문

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

예:

```bash
neverc update                 # 이 호스트용 최신 완전 release
neverc update v3389.1.2       # 정확한 태그(업/다운그레이드)
neverc update 3389.1.2        # 선행 v 생략 가능
neverc upgrade                # neverc update와 동일
```

`-y` / `--yes`는 스크립트 호환용입니다. 업데이트 자체는 비대화형입니다.

## 동기화 범위

| 구성 요소 | 동작 |
|-----------|------|
| 컴파일러(`bin/`, `lib/`, `pluginsdk/`) | 대상 태그가 다를 때 교체 |
| `runtime/`의 설치된 runtime | **이미 있는** 대상만 다시 받아 같은 태그로 고정 |
| 미설치 runtime | 자동 설치 안 함 — [`neverc runtime install`](../runtime/README.ko.md) |

## 안전 모델

1. `<install>/.neverc-update.lock` 배타 잠금.
2. 대상 태그 해석.
3. `SHA256SUMS`와 필요한 아카이브 다운로드·검증.
4. 스테이징에서 검증 후 커밋; 실패 시 롤백.

불량 runtime이면 이전 태그로 함께 되돌립니다:

```bash
neverc update v3389.0.1
```

## 제약

- 릴리스 설치 루트만(보통 `~/.neverc`). 파일시스템 루트와 CMake 빌드 트리는 거부.
- 호스트가 게시된 컴파일러 자산과 일치해야 함.
- Windows에서는 실행 중 `neverc.exe` 교체를 위해 짧은 헬퍼 프로세스를 쓸 수 있음.

## 관련 명령

| 명령 | 용도 |
|------|------|
| [`neverc runtime`](../runtime/README.ko.md) | 개별 sysroot만 관리 |
| [`neverc run`](../run/README.ko.md) | 호스트에서 임시 바이너리 컴파일·실행 |
| [`neverc build` / `make`](../build/README.ko.md) | Makefile 기반 빌드 |
| `neverc update --help` | 내장 도움말 |
