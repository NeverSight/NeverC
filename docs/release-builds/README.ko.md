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
| Android 커널 `.ko`(ELF ET_REL) | `.debug*`, `.comment`, 재배치에 필요 없는 로컬/undefined 항목, 보존된 일반 정의의 읽을 수 있는 이름 | `.strtab`에 연결된 하나의 `.symtab`, 모든 재배치와 대상, 정확한 로더/CFI 이름, 정확한 import, 보호 섹션 내 이름, 모듈 ABI 메타데이터 |
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

`neverc make release`는 권장 릴리스 명령이며 `-O2 --strip`으로 확장됩니다.
`.nvk-build-flags`가 없으면 `make`는 debug를 기본값으로 사용하고 스스로
release를 선택하지 않습니다. 예제 Makefile은 명시적인 프로필 선택을 저장하므로
이후 `make push`, `make run`, 대상 없는 `make`가 같은 산출물을 사용합니다.
`make debug` 또는 명시적인 `PROFILE=...`는 저장된 선택을 갱신하고,
`make clean`은 저장 상태를 삭제하여 다음 빌드를 debug로 되돌립니다. 이 최종
경로에서 NeverC는 디버그 섹션, `.comment`, 재배치에 불필요한 로컬/미정의
항목을 제거한 뒤 `.strtab`을 다시 구성합니다.

release가 성공하면 NeverC는 모듈 옆에 `<module>.ko.symbols.json`을 원자적으로
생성합니다. 이 파일은 이름이 변경된 보존 심볼마다 `original`(원래 이름)과
`release`(`.ko` 안의 이름)를 기록하며, `image_sha256`으로 최종 모듈 바이트에
바인딩하여 다른 버전의 맵을 잘못 사용하는 일을 방지합니다.

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 1,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

항목은 `release` 순으로 정렬됩니다. 제거된 심볼과 정확한 이름을 유지해야 하는
로더, import, CFI 이름은 변환이 필요 없으므로 기록하지 않습니다. debug 또는
기타 비-strip 빌드가 같은 출력 경로를 덮어쓰면 NeverC는 오래된 맵을 제거합니다.
맵에는 읽을 수 있는 원래 이름이 포함되므로 비공개 디버깅 산출물로 보관하고,
`.ko`와 함께 배포하거나 장치에 push하지 마십시오. 크래시 로그의 release 이름을
변환하기 전에 먼저 현재 `.ko`와 맵의 바인딩을 확인하십시오.

```bash
test "$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" = "$(jq -r '.image_sha256' nvk_hello.ko.symbols.json)"

jq -r '.symbols[] | select(.release == "fn_C000") | .original' \
  nvk_hello.ko.symbols.json
```

대상인 보존 정의에는 IDA에서 착안하되 예약 접두사를 사용하지 않는 결정적인 구조
이름을 부여합니다.

- `STT_FUNC`는 `fn_HEX`;
- `STT_OBJECT`는 `obj_HEX`;
- 실행 가능한 `STT_NOTYPE`는 `code_HEX`;
- 그 밖의 할당된 `STT_NOTYPE`는 `sym_HEX`;
- `SHN_ABS`는 `abs_HEX`;
- `SHF_ALLOC` 밖의 정의는
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`가 됩니다.

비할당 형식의 두 필드를 포함한 모든 `HEX` 필드는 불필요한 선행 0 없는
대문자 16진수입니다. 여러 심볼이 같은 표기를 필요로 하면 결정적인 10진 별칭
`_1`, `_2` 등을 덧붙입니다.

이 표기는 IDA에서 착안했지만 dummy-name 이름 공간을 차지하지 않습니다. 새 IDA
9.4 데이터베이스에서 ELF 사용자 심볼 `sub_0`, `sub_4`, `loc_8`은 각각
`_sub_0`, `_sub_4`, `_loc_8`로 표시되는 반면 `fn_0`, `code_8`, `obj_10`은
그대로 표시됩니다. Hex-Rays의
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) 문서도
`sub_` 같은 dummy 접두사로 시작하는 사용자 이름 앞에 `_`를 붙인다고 설명합니다.
NeverC는 IDA가 `sub_`를 합성하게 하려고 일반 정의의 `st_name`을 비우지 않습니다.
Android/Linux 모듈 kallsyms는 역사적으로 이름이 빈 항목을 무시했고, 빈 이름은
감사 가능한 직렬화 이름 계약도 없애기 때문입니다. 원래 비어 있어야 하는 항목과
섹션 심볼은 정확히 보존합니다.

ELF에서는 여러 심볼이 같은 canonical analysis EA를 공유할 수 있습니다.
NeverC는 `.symtab`에 전체 alias 집합을 보존하거나 생성하지만, IDA 9.4의 주소
이름 모델은 같은 주소의 심볼 중 하나의 주 이름만 구체화할 수 있습니다. 따라서
IDA에 표시되지 않은 alias가 ELF에서 사라졌다는 뜻은 아닙니다. 전체 집합은
`llvm-readelf` 또는 `llvm-nm`으로 감사해야 합니다.

할당된 심볼에서 `HEX`는 NeverC canonical analysis EA, 즉 정적 분석에만 쓰는
정규 유효 주소입니다. 커서 0에서 시작해 최종 섹션 헤더 순서대로 최종 보존
`SHF_ALLOC` 섹션을 방문하고, 커서를 `max(sh_addralign, 1)`에 맞춘 값을 섹션
기준으로 기록한 뒤 `max(sh_size, 1)`만큼 전진합니다. EA는 이 기준과 최종
`st_value`의 합입니다. `abs_HEX`는 최종 절대 `st_value`를 사용합니다.
비할당 형식에서 `FINAL_SECTION_ORDINAL_HEX`는 최종 섹션 순번이고
`OFFSET_HEX`는 해당 섹션의 최종 `st_value`입니다. 이 좌표는 해시값이나 암호화
결과가 아니며 파일 오프셋, ELF 가상 주소, 커널 런타임 주소도 아닙니다. 로더와
KASLR은 런타임에 모듈을 다른 위치에 배치할 수 있습니다.

다음 이름은 정확히 보존합니다.

- 모듈 로더가 이름으로 해석하는 모든 `SHN_UNDEF` import.
- `.modinfo`, `.text.ftrace_trampoline`, `.gnu.linkonce.this_module`,
  `__versions`, `.codetag.alloc_tags` 안에 정의된 심볼.
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module`, `__cfi_jt_cleanup_module`.
- `__typeid__` 또는 `__kcfi_typeid_`로 시작하는 이름.

IDA의 `extern` 영역은 분석기가 합성한 보기일 뿐 실제 ELF 섹션이 아닙니다. 최종
`ET_REL` `.ko`에서 외부 재배치 대상은 `.symtab`의 `SHN_UNDEF` 항목이며, 로더는
그 정확한 이름을 필요로 합니다. 따라서 정책은 실제 ELF 심볼 클래스와 정의 섹션을
따릅니다. 미정의 가져오기는 원래 이름을 보존하고, 대상 정의는 분석 도구의 분류와
관계없이 이름을 바꿉니다.

모든 이름은 변경 전에 전역으로 계획합니다. 같은 기본 후보를 공유하는 정의에는
결정적인 순서로 번호 없는 형식, `_1`, `_2` 등을 부여합니다. 이처럼 정상적으로
이름을 배정하는 경우는 오류가 아닙니다. 생성 이름이 그대로 보존할 이름의 예약
영역과 충돌하거나 좌표 또는 접미 번호 계산이 수치 범위를 넘으면 확정 처리를
중단합니다.
`SHN_COMMON`, `SHN_LIVEPATCH` 또는 알 수 없는 ELF 예약 섹션 인덱스를 만나도
추측하지 않고 안전하게 거부합니다. 로드 가능한 최종 모듈에는
`SHN_COMMON`이 유효하지 않으므로 `-fno-common`으로 컴파일하십시오.
Livepatch 모듈에는 원래 심볼 테이블 순서와 인덱스 및 추가 재배치 메타데이터가
필요하며, 이 릴리스 정책은 이를 보존한다고 주장하지 않습니다.

탐지는 여러 신호를 사용합니다. `SHN_LIVEPATCH` 심볼, `.klp.*` 섹션,
`SHF_RELA_LIVEPATCH` 플래그 또는 NUL로 구분된 `.modinfo`의 `livepatch=`로
시작하는 필드 중 하나라도 있으면 livepatch 모듈로 판단해 안전하게
거부합니다. `.klp.*` 섹션이나 livepatch 재배치 플래그가 없어도 이 `.modinfo`
마커만으로 거부하기에 충분합니다.

대상인 `.symtab` 이름만 바뀝니다. 로드 가능한 `.ko`에는 여전히 `.symtab`,
연결된 `.strtab`, 재배치가 필요하므로 일반 도구가 `not stripped`라고 표시해도
정상입니다. BTF, 모듈 export, `.modinfo`, `__versions`, trace metadata,
`__ksymtab_strings`, `.rodata`, 문자열 리터럴 같은 독립 저장소와 인터페이스는
원래 이름이나 식별 텍스트를 계속 노출할 수 있습니다. 일반 커널 심볼 이름은
kallsyms와 진단에서도 바뀌므로 심볼 기반 ftrace, kprobe/BPF attach, 크래시
보고서의 유용성이 낮아집니다. 진단에는 스트립하지 않은 debug 빌드를 사용하고
release 모듈에서 private 심볼의 원래 이름에 의존하지 마십시오.

### 최종 Android release의 plugin 경계

finalization은 plugin 출력 단계 양쪽에 서로 독립적인 fail-closed identity 경계를
두 단계로 설정합니다.

- 교체 가능한 `ObjectGraph` 단계 전에 graph identity seal은 유지되는 각 logical
  section의 `section ID`, `final ordinal`, 정확한 이름을 고정합니다. 또한 정확한
  이름을 유지해야 하는 각 심볼의 `symbol ID`를 이름, class, section, value, size,
  binding, type, 전체 `st_other`에 연결합니다. 일반 구조 이름은 release verifier가
  별도로 다시 계산합니다.
- host가 신뢰할 수 있는 write baseline을 설정한 뒤
  `neverc.object.post_write` 전에 image identity seal은 유지되는 각 logical
  section의 ordinal/name, 전체 `.symtab` entry 수, 각 exact-name symbol의 이름과
  속성을 raw `.symtab` `slot`에 고정합니다.

따라서 capability matrix는 의도적으로 좁습니다.

| 단계 binding | 최종 Android release 동작 |
|--------------|---------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED`; host가 설정한 신뢰 write baseline을 교체하기 전에 거부 |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; 최종 Android release에는 신뢰 baseline을 설정하는 host-owned graph writer가 필요 |
| `observer` | `READ_ONLY`; 관찰은 허용되지만 artifact 수정은 불가 |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; identity 영역 밖의 payload byte만 변경할 수 있고 release verifier, 입력 ABI contract, 두 identity seal을 모두 계속 통과해야 함 |

최종 merge의 소유권도 host가 봉인합니다. `third-party ObjectMergeProvider`가 반환한
`MergedImage` 또는 독립 byte는 폐기하고, 검증 및 finalize된 graph를
`host-owned graph writer`가 직렬화합니다. 반대로
`built-in finalized input serialization`은 `external object phases`를 우회하여 정확한
`audited native bytes`를 host merger에 전달합니다. 이 내부 입력 단계는 위의 출력
경계를 우회하지 않습니다.

Finalization은 `Android module merge semantics`에서만 허용됩니다.
`relocatable output request`와 `relocatable driver configuration`도 모두 필요하며,
그렇지 않으면 `before routing`에 실패합니다. 최종 Android relocatable release에서는
`frozen input format`,
`TargetKey.ObjectFormatID`, `frozen output format`이 `one format identity`를
공유해야 합니다. 불일치는 `before provider dispatch`, 즉 route planning과 sink
creation보다도 먼저 거부되므로 capability preflight와 실제 graph-writer dispatch가
서로 다른 format을 볼 수 없습니다.

일반적인 graph-representable input에서는 앞선 graph interceptor가 graph seal과 모든
release semantics를 유지할 때만 실행될 수 있습니다. `ObjectGraph`가 표현할 수 없는
사실 때문에 native-image passthrough가 필요한 input에서는 교체 가능한 모든
`route-matching provider`와 모든 interceptor가 거부됩니다.
target/CPU/features/object-format/execution-level route가 일치하지 않는 provider는 실행되지
않고 release도 막지 않으며, read-only observer만 허용됩니다. `before sealed commit`
시점의 거부 또는 검증 실패만 staging을 중단하고 파일을 게시하지 않습니다.
`AFTER_COMMIT` observer 실패는 게시 후 보고되며 이미 게시된 파일을 되돌릴 수 없습니다.

`.ko`를 `llvm-strip --strip-all`이나 `objcopy`로 후처리하거나 codetag/BTF/ABI
섹션을 무작정 제거하지 마십시오. 서명할 경우 먼저 스트립한 뒤 최종 바이트에
서명하십시오. 서명 뒤의 변경은 서명을 무효화합니다. `clean`은 파일만 삭제해야
하며 기존 모듈을 스트립하거나 서명해서는 안 됩니다.

## 보안 경계

스트립은 가치가 높은 이름과 디버그 메타데이터를 제거해 분석 비용을 높이지만
네이티브 기계어의 리버스 엔지니어링을 불가능하게 만들지는 못합니다. 올바르게
스트립한 바이너리에도 다음이 남을 수 있습니다.

- 로더가 요구하는 동적 가져오기 및 내보내기 이름.
- `.ko`의 로더 필수 이름과 `.symtab` 밖에 저장된 이름.
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
아래의 부정 `strings` 검사는 일치 항목이 없어야 하며 그때만 성공으로 종료됩니다.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

로드 가능한 ELF `ET_REL` `.ko`는 `.symtab`을 의도적으로 보존하므로 일반
`file` 도구가 `not stripped`라고 표시할 수 있습니다. 이 표시로 release 성공
여부를 판단하지 마십시오. 대신 DWARF와 `.comment`가 없고, 대상 정의가 정규
대문자 16진 형식 `fn_`/`obj_`/`code_`/`sym_`/`abs_`를 사용하며,
`SHN_UNDEF` import와 필수 로더/CFI 이름이 정확히 유지되고 재배치가 유효한지
확인하십시오. 이름 노출이 중요하면 BTF, export, modinfo, versions, trace
metadata, 문자열도 별도로 점검하십시오.

스트립된 산출물에는 소스 수준 디버그 섹션이나 비공개 정적 심볼 이름이 없어야
합니다. 필요한 동적 이름과 런타임 메타데이터는 정상적인 결과입니다.
