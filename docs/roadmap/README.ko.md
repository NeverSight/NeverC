**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 인덱스](../README.ko.md)

# NeverC 로드맵

이 문서는 기존 shellcode 컴파일러와 내장 런타임을 넘어선 NeverC 프로젝트의 주요 계획 방향을 설명합니다.

---

## 1. 표준 라이브러리 (`std`)

NeverC는 Go의 표준 라이브러리를 모델로 한 포괄적인 표준 라이브러리를 제공합니다 — 외부 의존성 없이 일반적인 시스템 프로그래밍 요구를 충족하는 배터리 포함 패키지입니다.

### 계획된 패키지

| 패키지 | 설명 |
|--------|------|
| `fmt` | 서식 있는 I/O (printf 계열 + 타입 안전 확장) |
| `os` | OS 상호작용: 환경 변수, 프로세스 관리, 파일 권한 |
| `io` | Reader/Writer 인터페이스, 버퍼링 I/O, 파이프 유틸리티 |
| `fs` | 파일시스템 작업: 워크, glob, 임시 파일, 원자적 쓰기 |
| `net` | TCP/UDP 소켓, DNS 해석, HTTP 클라이언트/서버 |
| `net/http` | HTTP/1.1 및 HTTP/2 클라이언트와 서버 |
| `crypto` | 해시 (SHA-256, SHA-512, BLAKE3), HMAC, AES, ChaCha20, RSA, Ed25519 |
| `encoding` | JSON, Base64, Hex, CSV, 바이너리 (리틀/빅 엔디안) |
| `sync` | Mutex, RWLock, WaitGroup, Once, 원자적 연산 |
| `time` | 모노토닉/벽시계, 지속 시간, 타이머, 포맷팅 |
| `strings` | 검색, 분할, 결합, 트림, 교체, 빌더 |
| `bytes` | 바이트 슬라이스 조작, 버퍼 |
| `math` | 수학 상수, 기본 함수, 난수 생성 |
| `sort` | 제네릭 정렬 및 검색 |
| `container` | 연결 리스트, 힙, 링 버퍼 |
| `log` | 레벨이 있는 구조화된 로깅 |
| `flag` | 명령줄 플래그 파싱 |
| `path` | 경로 조작 (POSIX 및 Windows) |
| `regexp` | 정규 표현식 매칭 (RE2 구문) |
| `compress` | gzip, zlib, zstd, lz4 |
| `hash` | CRC32, CRC64, FNV, xxHash |
| `unicode` | Unicode 테이블, 대소문자 폴딩, UTF-8/UTF-16 변환 |

### 설계 원칙

- **순수 C23** — 모든 패키지가 표준 NeverC/C23로 컴파일; 숨겨진 C++이나 플랫폼 특정 어셈블리 없음
- **외부 의존성 제로** — 표준 라이브러리는 기존 `string` 및 `mimalloc` 내장과 마찬가지로 LLVM bitcode로 컴파일러에 임베디드
- **크로스 플랫폼** — 모든 패키지가 macOS, Linux, Windows (x86_64 / AArch64)에서 작동
- **Shellcode 호환** — 프리스탠딩 모드에서 의미 있는 패키지 (예: `crypto`, `encoding`, `strings`)는 `-fshellcode`에서 작동

---

## 2. UI 컴포넌트 라이브러리 (`neverc-ui`)

NeverC는 Qt에서 영감을 받은 크로스 플랫폼 UI 컴포넌트 라이브러리를 제공합니다 — 단, HTML/JS/CSS 프론트엔드 렌더링 엔진을 채택하여 AI 인터페이스 설계에 본질적으로 적합합니다.

### 목표

- **컴포넌트 기반 아키텍처** — 윈도우, 버튼, 텍스트 입력, 리스트, 트리, 테이블, 메뉴, 다이얼로그, 탭, 레이아웃 컨테이너를 C의 일급 타입으로 제공
- **HTML/JS/CSS 렌더러** — 내장 경량 브라우저 엔진으로 UI 렌더링; 개발자는 C 로직을 작성하고 비주얼 레이어는 표준 웹 기술 사용
- **드래그 앤 드롭 비주얼 디자이너** — NeverC 호환 C 코드를 생성하는 GUI 빌더; 레이아웃 코드 수작업 없이 빠른 프로토타이핑
- **AI 네이티브 디자인 워크플로** — LLM이 C 비즈니스 로직과 HTML/CSS 레이아웃을 한 번에 생성 가능; 비주얼 레이어는 세계에서 가장 널리 이해되는 UI 언어 사용
- **네이티브 룩 앤 필** — CSS 변수와 시스템 폰트/색상 감지를 통한 플랫폼 적응 테마 (macOS, Windows, Linux)
- **경량 임베딩** — 렌더러는 내장 런타임으로 제공 (`string` / `mimalloc`과 유사); Electron 수준의 오버헤드 없음
- **이벤트 시스템** — 사용자 인터랙션 (클릭, 입력, 리사이즈, 드래그, 키보드, 커스텀 이벤트)용 C 콜백 함수
- **데이터 바인딩** — C 구조체와 UI 상태 간 선언적 바인딩; 변경 사항 자동 전파
- **커스텀 렌더링** — 게임 UI, 데이터 시각화, 커스텀 위젯을 위한 raw canvas/WebGL 이스케이프 해치

### 왜 C UI 라이브러리에 HTML/CSS를?

- 모든 AI 모델이 이미 HTML/CSS를 알고 있다 — UI 코드 생성에 전문 훈련 불필요
- 웹 기술은 가장 검증된 레이아웃 시스템; flexbox, grid, 텍스트 렌더링을 재발명할 필요 없음
- 보안 연구 도구 (대시보드, 헥스 뷰어, 패킷 인스펙터)는 독점 위젯 API를 배우지 않고도 풍부한 스타일 인터페이스 활용
- 비주얼 디자이너가 내보내는 HTML 템플릿은 NeverC 앱과 독립 브라우저 모두에서 동작하여 빠른 반복 가능

---

## 3. EVM 스마트 컨트랙트 백엔드

NeverC는 C 소스 코드를 EVM (Ethereum Virtual Machine) 바이트코드로 컴파일하는 것을 지원합니다 — 개발자가 Solidity 대신 C로 스마트 컨트랙트를 작성할 수 있게 합니다.

### 목표

- **새 LLVM 백엔드 타겟** — `evm` 타겟 트리플 (예: `neverc --target=evm hello.c -o contract.bin`)
- **ABI 호환** — Solidity 호환 ABI 디스크립터 생성, 기존 이더리움 도구 (Hardhat, Foundry, ethers.js)와 연동
- **스토리지 레이아웃** — C 구조체를 결정적 레이아웃으로 EVM 스토리지 슬롯에 매핑
- **내장 EVM 프리미티브** — `msg.sender`, `msg.value`, `block.number`, `tx.origin`을 내장 변수 또는 인트린식으로 제공
- **payable / view / pure 수정자** — Solidity 가시성 시맨틱에 매핑되는 함수 속성
- **이벤트 발행** — 어노테이션된 함수 호출에서 `LOG0`–`LOG4` 옵코드 생성
- **Gas 최적화** — gas 비용을 최소화하는 IR 패스 (스택 스케줄링, 상수 폴딩, 데드 스토리지 제거)
- **revert / require** — 커스텀 에러 메시지가 있는 에러 처리 프리미티브

### 왜 C로 EVM을?

- Solidity 구문은 JavaScript 개발자에게 익숙하지만 시스템 프로그래머에게는 낯설다; C는 보편적
- NeverC의 기존 IR 최적화 파이프라인은 많은 경우 `solc`보다 더 컴팩트한 바이트코드 생성 가능
- 보안 연구자는 이미 C로 사고한다 — C 컨트랙트에 대한 감사 도구와 fuzzer를 C로 작성하는 것이 자연스러움
- 플러그인 API로 컴파일 시 커스텀 gas 분석 및 취약점 탐지 패스 가능

---

## 4. Solana eBPF 백엔드

NeverC는 C 소스 코드를 Solana의 eBPF 바이트코드로 컴파일하는 것을 지원합니다 — C로 온체인 프로그램 개발을 실현합니다.

### 목표

- **eBPF 타겟** — `sbf` (Solana BPF) 타겟 트리플 (예: `neverc --target=sbf-solana hello.c -o program.so`)
- **Solana 런타임 바인딩** — Solana 시스콜용 내장 헤더: `sol_invoke_signed`, `sol_log`, `sol_memcpy`, 계정 정보 구조체
- **계정 모델** — C 구조체로 Solana 계정 데이터 오버레이, 자동 직렬화/역직렬화
- **CPI (크로스 프로그램 호출)** — 다른 온체인 프로그램 호출을 위한 타입 안전 래퍼
- **PDA (프로그램 파생 주소)** — PDA 도출 및 검증을 위한 내장 함수
- **컴퓨트 버짓 인식** — 예상 컴퓨트 유닛이 프로그램 한도를 초과하면 컴파일러 경고
- **Anchor 호환** — Anchor 기반 프론트엔드와의 상호운용을 위한 선택적 IDL 생성

### 왜 C로 Solana를?

- Solana 런타임은 eBPF를 실행한다 — C는 BPF 타겟의 가장 자연스러운 소스 언어
- 기존 C 기반 BPF 도구 체인 (clang + solana-bpf)은 설정이 복잡; NeverC는 모든 것을 단일 바이너리에 번들
- 성능이 중요한 프로그램은 C의 제로 오버헤드 추상화와 NeverC의 최적화 패스에서 이점
- shellcode 컴파일 경험 (위치 독립, 최소 런타임 코드)이 온체인 프로그램 제약에 직접 매핑

---

## 타임라인

이 기능들은 연구 및 설계 단계에 있습니다. 구체적인 릴리스 날짜는 미정입니다. 진행 상황은 이 문서에서 업데이트되며 프로젝트 릴리스 페이지에서 발표됩니다.

| 기능 | 상태 |
|------|------|
| 표준 라이브러리 (`std`) | 연구 / 설계 |
| UI 컴포넌트 라이브러리 (`neverc-ui`) | 연구 / 설계 |
| EVM 스마트 컨트랙트 백엔드 | 연구 / 설계 |
| Solana eBPF 백엔드 | 연구 / 설계 |
