**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode 컴파일러](../README.ko.md)

# Shellcode 플러그인 인터페이스 (Plugin SDK)

NeverC의 shellcode 파이프라인은 **코어 파이프라인 + 플러그인 가능 사용자 계층** 이중 구조입니다. 난독화, 안티 디스어셈블리, EDR 회피, 단계 인코더 등 전략 계층 기능은 **의도적으로 내장하지 않습니다**.

## 1. Finalize 파이프라인
`finalizeShellcodeBytes`가 순차 처리: PostExtract 훅 → 금지 바이트 리라이터 체인 → 캐릭터셋 인코더 → 금지 바이트 감사 → 크기 제약 → PostFinalize 훅.

## 2. 금지 바이트 리라이터
`registerBadByteRewriteStrategy`로 전략 등록. 멱등, 결정적, 바이트 스트림만 참조.

## 3. 캐릭터셋 인코더
`registerCharsetEncoder`로 `(Name, Encode, Stub, IsCharsetMember)` 튜플 등록. 출력은 캐릭터셋 검증 통과 필수.

## 4. 크기 / 정렬 / 패딩
`-fshellcode-max-length=`, `-fshellcode-align=`, `-fshellcode-pad=`. 플러그인 불필요.

## 5. 3계층 훅 매핑
IR 계층 6개 훅, MIR 계층 3개 훅, 바이트스트림 계층 2개 훅.

## 6. 등록 위치 선택 + PIC 커버리지 매트릭스
이른 등록 = 넓은 내장 PIC 수정 커버리지. 추천: 문자열 암호화는 `RunAfterPrep`, CFF는 `RunAfterInlining`, 명령 치환은 `RunAfterPreEmit`, 전체 페이로드 암호화는 `RunPostFinalize`. 다중 라이브러리 공존은 get/modify/set 패턴.
