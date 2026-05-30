**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../docs/i18n/README.ko.md)

# NeverC 예제

NeverC의 크로스 플랫폼 컴파일 기능을 보여주는 빌드 가능한 예제. 모두 macOS / Linux에서 크로스 컴파일 가능 — Windows 빌드 환경 불필요.

---

## 예제 목록

| 예제 | 설명 | 주요 기능 |
|------|------|---------|
| [Windows 커널 드라이버](../../examples/windows-driver/README.ko.md) | 최소 WDM 커널 드라이버 | macOS/Linux에서 `.sys` 크로스 컴파일, 자동 LTO, 내장 링커 |
| [Windows 드라이버 + CET](../../examples/windows-driver-cet/README.ko.md) | Intel CET 섀도 스택 커널 드라이버 | CET 호환 커널 코드, `/guard:ehcont` |
| [Windows 드라이버 + 부동 소수점](../../examples/windows-driver-float/README.ko.md) | 부동 소수점/SIMD 커널 드라이버 | 커널 모드 안전 부동 소수점 |

---

## 빠른 시작

```bash
cd examples/<예제명>
make
```

컴파일러 경로 지정: `make NEVERC=/path/to/neverc`

모든 예제는 **neverc**를 컴파일러로 사용하며 내장 링커를 통해 Windows PE 바이너리(`.sys`)를 생성합니다.
