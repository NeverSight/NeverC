**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# mTLS 안티치트 텔레메트리 수집기

이 실행 가능한 수집기는 클라이언트 인증서를 필수로 하는 TLS 1.3 위에서
다중화 NRPC 프로토콜을 제공합니다. `anticheat.Telemetry/Collect`는
양방향 스트림입니다. 에이전트는 서명된 텔레메트리 레코드를 보내고,
수락된 각 레코드에 대해 nonce ACK를 하나 받습니다.

각 DATA 메시지는 64바이트 헤더와 불투명 본문(최대 1 MiB)으로 구성됩니다.
헤더에는 버전 `1`, 세 개의 0바이트, 8바이트 빅엔디안 Unix 밀리초
타임스탬프, 16바이트 nonce, 4바이트 빅엔디안 본문 길이, 32바이트
HMAC-SHA256이 포함됩니다. MAC은
`agent-id || first-32-header-bytes || body`를 커버합니다. NRPC 메타데이터
값 `agent-id`는 필수입니다. nonce는 30초 시계 창 안에서 한 번만
수락됩니다.

수락된 레코드는 경계가 있는 큐에 들어갑니다. 전용 단일 작성자가
클라이언트 인증서 지문, nonce, 본문 다이제스트, 타임스탬프, 본문 크기를
포함하는 JSONL 감사 이벤트를 추가합니다. 수집기는 신뢰할 수 없는 본문
원시 바이트를 감사 로그에 직접 쓰지 않습니다.

호스트 또는 지원되는 모든 대상용으로 빌드:

```bash
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 되돌리기
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Makefile이 `PROFILE`을 유지하므로 이후 `neverc make`는 같은
debug/release 선택을 사용합니다. 릴리스는 NeverC 내장 `--strip`을 사용합니다.
[릴리스 빌드](../../docs/release-builds/README.ko.md)를 참고하세요.


서버 인증서, 서버 키, 신뢰하는 클라이언트 CA, 공유 32바이트 서명 키,
감사 경로를 지정하여 실행:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
