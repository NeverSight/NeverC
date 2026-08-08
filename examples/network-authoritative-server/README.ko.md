**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 예제](../../docs/examples/README.ko.md)

# 권위형 게임 서버

이 실행 가능한 예제는 원시 플랫폼 소켓 대신 NeverC의 이식 가능한 네트워킹
스택을 사용합니다. 제공 기능:

- CSPRNG 기반 세션 토큰을 발급하는 TCP 제어 평면;
- UDP 및 네이티브 QUIC 실시간 입력 평면;
- 60 Hz 권위형 시뮬레이션 루프와 경계가 있는 입력 큐;
- 가입 요청용 타임스탬프/nonce 재생 방지 및 세션별 단조 증가 입력
  시퀀스 번호.

호스트용으로 빌드하거나 지원되는 NeverC 대상 트리플을 설정:

```bash
neverc make          # debug: -g(첫 빌드 기본값)
neverc make release  # release: -O2 --strip
neverc make debug    # debug로 되돌리기
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Makefile이 `PROFILE`을 유지하므로 이후 `neverc make`는 같은
debug/release 선택을 사용합니다. 릴리스는 NeverC 내장 `--strip`을 사용합니다.
[릴리스 빌드](../../docs/release-builds/README.ko.md)를 참고하세요.


QUIC 엔드포인트용 P-256 TLS 인증서와 키로 실행:

```bash
./authoritative-server cert.pem key.pem
```

선택적 주소 기본값은 TCP `:7000`, UDP `:7001`, QUIC `:7002`입니다.
TCP 가입 줄은 `JOIN <client-id> <unix-ms> <32-hex-nonce>`입니다. UDP 입력은
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`입니다. QUIC 클라이언트는
`neverc-game/1`을 협상하고, 첫 스트림에서 `AUTH <token>`으로 인증한 뒤
`<sequence> <dx> <dy>`를 포함하는 데이터그램을 보냅니다.
