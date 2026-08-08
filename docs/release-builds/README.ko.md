**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서 색인](../README.ko.md) · [← NeverC 프로젝트](../../README.md)

# 릴리스 바이너리와 `--strip`

배포할 실행 파일, 공유 라이브러리 또는 최종 Android 커널 모듈을 만들 때
`--strip`을 사용합니다. 짧은 별칭은 `-s`이며 두 표기의 동작은 같습니다.

## 빠른 시작

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC는 통합 링커 내부에서 스트립하며 외부 `llvm-strip`을 실행하지 않습니다.
따라서 같은 명령으로 교차 대상 ELF, Mach-O, PE/COFF 출력을 만들 수 있습니다.

이 CLI 옵션을 CMake 패키징 스위치 `NEVERC_STRIP_BINARY`와 혼동하지 마십시오.
후자는 빌드 후 `neverc` 컴파일러 실행 파일만 처리하며 외부 strip 도구를
호출할 수 있습니다. NeverC가 컴파일한 프로그램에는 영향을 주지 않습니다.

## 디버그 정보와 심볼 정책

| 호출 | 소스 수준 디버그 정보 | 일반 정적 심볼 이름 | Darwin `.dSYM` |
|------|-----------------------|---------------------|----------------|
| 기본값(`-g` 없음) | 생성하지 않음 | 남을 수 있으며 정확한 기본값은 형식에 따라 다름 | 생성하지 않음 |
| `-g` | 생성 | 유지 | 일반 Darwin 링크에서 생성 |
| `--strip` | 있으면 제거 | 런타임에 필요 없는 이름 제거 | 생성하지 않음 |
| `-g --strip` | 스트립 정책이 우선하며 배포 이미지에는 없음 | 런타임에 필요 없는 이름 제거 | 생성 억제 |

`-g`가 없으면 프런트엔드는 소스 수준 디버그 정보를 생성하지 않습니다. 하지만
출력이 완전히 스트립되었다는 뜻은 아닙니다. ELF와 Mach-O에는 일반 심볼 이름이
남을 수 있고, PE는 디버그 설정이 요구하지 않는 한 보통 정적 COFF 심볼 테이블이
없습니다. Auto-LTO가 일부 로컬 이름을 버릴 수 있지만 strip-all 보장은 아닙니다.

`-g`는 디버그 없음에서 소스 수준 디버그 생성으로 정책을 바꿉니다. 기본 디버그
정보 위에 “더 많은 정보”를 추가하는 옵션이 아닙니다. ELF/Mach-O의 `.eh_frame`과
PE의 `.pdata`/`.xdata`는 런타임 메타데이터이지 소스 수준 DWARF가 아니므로
스트립된 이미지에도 남을 수 있습니다.

## 구현과 형식별 동작

드라이버는 `--strip`을 하나의 강타입 링커 정책으로 바꾸어 세 백엔드에 전달합니다.
각 백엔드는 형식을 이해하는 단계에서 정책을 적용하고 로더나 동적 ABI에 필요한
이름과 레코드는 보존합니다.

| 형식 | 제거 | 필요할 때 보존 |
|------|------|----------------|
| ELF | `.debug*` 데이터와 일반 정적 심볼/문자열 테이블 | 동적 가져오기/내보내기, 재배치와 로더 메타데이터, 언와인드 정보 |
| Android 커널 `.ko`(ELF ET_REL) | `.debug*`, `.comment`, 보존된 재배치에 필요 없는 로컬/undefined 심볼 | `.strtab`에 연결된 하나의 `.symtab`, 모든 재배치와 대상, 정의된 전역 심볼, import, `__versions`, `.codetag.alloc_tags`, 모듈 ABI |
| Mach-O | 디버그 맵/STABS, 런타임에 필요 없는 로컬/전역 심볼 항목, 동반 `.dSYM` 생성 | 바인딩/가져오기 데이터, 내보낸 ABI 이름, export trie 항목, 런타임 참조 심볼 |
| PE/COFF | 내장 DWARF 섹션과 존재하는 정적 COFF 심볼/문자열 테이블 | PE 가져오기/내보내기, 언와인드 테이블, 로드 구성과 기타 로더 메타데이터 |

## 범위와 우선순위

- `--strip`은 최종 링크된 실행 파일, 공유 라이브러리와 아래의 엄격한 최종
  Android `.ko` 예외를 지원합니다.
- `-c`, 일반 `-r`, Android 중간 `.o`, `--emit-static-lib`, `-fdyncode`와
  함께 사용하면 명확히 오류를 냅니다.
- 스트립 정책은 `-g`와 백엔드 디버그 스위치보다 우선합니다.
- NeverC 기본 Auto-LTO 파이프라인과 `-fno-lto`를 모두 검증합니다.
- 제거하면 동적 ABI가 깨지는 공유 라이브러리 가져오기/내보내기 이름은 유지합니다.

## Android 커널 모듈

최종 `.ko`도 ELF `ET_REL`이며 Linux 모듈 로더는 심볼 테이블, 연결된 문자열
테이블, undefined import와 재배치를 요구하므로 strip-all 결과를 거부합니다.
NeverC는 Android 대상에서 `-fandroid-kernel-driver-mode`와 `-r`이 켜지고
출력 이름이 `.ko`로 끝날 때만 `-r --strip`을 허용합니다. 일반 `-r`과 중간
`.o`는 계속 거부됩니다.

이 경로는 `--strip-all`이 아니라 `llvm-strip --strip-unneeded`의 안전 경계를
구현합니다. 디버그, `.comment`, 재배치에 필요 없는 로컬/undefined 심볼을
제거하고 `.strtab`을 다시 만듭니다. `.symtab`, 모든 재배치와 필수 대상,
정의된 비로컬 심볼, import, `__versions`, `.codetag.alloc_tags`,
`.gnu.linkonce.this_module`은 보존합니다. `.ko`에
`llvm-strip --strip-all`을 사용하거나 codetag 섹션을 무작정 제거하지
마십시오. 스트립 후 최종 바이트에 서명하고 `clean`은 파일만 삭제해야 합니다.

## 보안 경계

스트립은 가치가 높은 이름과 디버그 메타데이터를 제거해 분석 비용을 높이지만
**난독화가 아니며**, 네이티브 기계어의 리버스 엔지니어링을 불가능하게 만들지
못합니다. 올바르게 스트립한 바이너리에도 다음이 남을 수 있습니다.

- 로더가 요구하는 동적 가져오기 및 내보내기 이름.
- `.ko`의 보존된 재배치에 필요한 심볼 이름.
- 문자열 리터럴, 리플렉션 테이블, 애플리케이션 정의 메타데이터.
- 언와인드, 재배치, 서명, 로드 구성 레코드.
- 기계어와 관찰 가능한 제어 흐름.

`--strip`은 최종 이미지에만 적용됩니다. 명시적으로 요청한 링크 맵, 최적화
레코드, `-save-temps` 출력 같은 별도 산출물은 삭제하지 않으므로 릴리스
디렉터리를 점검하고 이러한 부속 파일을 배포하지 마십시오.

필요하면 문자열 암호화, 난독화, 변조 방지를 별도 계층으로 사용하고, 기밀이어야
하는 비밀을 클라이언트 바이너리에 넣지 마십시오.

## 산출물 검증

CI에서 LLVM 오브젝트 도구로 릴리스 산출물을 검사할 수 있습니다. 대상 형식에
맞게 명령을 조정하고 프로그램에 필요한 ABI 이름을 명시적으로 허용하십시오.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM

llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

스트립된 산출물에는 소스 수준 디버그 섹션이나 비공개 정적 심볼 이름이 없어야
합니다. 필요한 동적 이름과 런타임 메타데이터는 정상적인 결과입니다.
