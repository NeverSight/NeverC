**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../../README.md)

# リリースバイナリと `--strip`

配布用の実行ファイル、共有ライブラリ、または最終 Android カーネルモジュールを
作るときは `--strip` を使います。短い別名は `-s` で、両者の動作は同一です。

## クイックスタート

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC は統合リンカー内部でストリップを行い、外部の `llvm-strip` を起動
しません。同じコマンドでクロスターゲットの ELF、Mach-O、PE/COFF を
生成できます。

この CLI オプションを CMake のパッケージ用スイッチ
`NEVERC_STRIP_BINARY` と混同しないでください。後者はビルド後に
`neverc` コンパイラ実行ファイルだけを処理し、外部の strip ツールを
呼び出す場合があります。NeverC が生成するプログラムには影響しません。

## デバッグ情報とシンボルの方針

| 呼び出し | ソースレベルのデバッグ情報 | 通常の静的シンボル名 | Darwin `.dSYM` |
|----------|----------------------------|----------------------|----------------|
| 既定（`-g` なし） | 生成しない | 残る場合がある。既定値は形式依存 | 生成しない |
| `-g` | 生成する | 残る | 通常の Darwin リンクで生成する |
| `--strip` | 存在すれば削除 | 実行時に不要な名前を削除 | 生成しない |
| `-g --strip` | ストリップ方針が優先され、配布イメージには残らない | 実行時に不要な名前を削除 | 生成を抑止 |

`-g` がなければ、フロントエンドはソースレベルのデバッグ情報を生成しません。
ただし、出力が完全にストリップ済みという意味ではありません。ELF と
Mach-O には通常のシンボル名が残り得ます。PE はデバッグ設定が要求しない
限り静的 COFF シンボル表を通常持ちません。Auto-LTO が一部のローカル名を
破棄しても、strip-all の保証にはなりません。

`-g` は「デバッグなし」から「ソースレベルのデバッグあり」へ切り替えます。
既定で存在する情報に「さらに追加」するものではありません。ELF/Mach-O の
`.eh_frame` や PE の `.pdata`/`.xdata` は実行時メタデータであり、
ソースレベル DWARF ではないため、ストリップ後も残り得ます。

## 実装と形式ごとの動作

ドライバーは `--strip` を単一の型付きリンカー方針へ変換し、3 つの
バックエンドへ渡します。各バックエンドは形式を理解している段階で方針を
適用し、ローダーまたは動的 ABI に必要な名前と記録を保持します。

| 形式 | 削除するもの | 必要な場合に保持するもの |
|------|--------------|--------------------------|
| ELF | `.debug*` データと通常の静的シンボル表／文字列表 | 動的インポート／エクスポート、再配置とローダーメタデータ、アンワインド情報 |
| Android カーネル `.ko`（ELF ET_REL） | `.debug*`、`.comment`、再配置に不要なローカル／未定義エントリ、保持された通常定義の可読名 | `.strtab` にリンクする 1 個の `.symtab`、全再配置と対象、正確なローダー／CFI 名、正確な import、保護セクション内の名前、モジュール ABI メタデータ |
| Mach-O | デバッグマップ／STABS、実行時不要のローカル／グローバルシンボル、付随する `.dSYM` 生成 | バインド／インポート情報、公開 ABI 名、export trie、実行時参照シンボル |
| PE/COFF | 埋め込み DWARF セクションと、存在する静的 COFF シンボル表／文字列表 | PE インポート／エクスポート、アンワインド表、ロード設定などのローダーメタデータ |

## 適用範囲と優先順位

- `--strip` は最終リンク済み実行ファイル、共有ライブラリ、および下記の
  厳密な最終 Android `.ko` 例外を対象にします。
- `-c`、通常の `-r`、Android 中間 `.o`、`--emit-static-lib`、`-fdyncode`
  との組み合わせは明示的にエラーにします。
- ストリップ方針は `-g` とバックエンドのデバッグスイッチより優先されます。
- NeverC 既定の Auto-LTO と `-fno-lto` の両方を対象にテストしています。
- 共有ライブラリの動的 ABI を壊すインポート／エクスポート名は保持します。

## Android カーネルモジュール

最終 `.ko` も ELF `ET_REL` であり、Linux モジュールローダーはシンボル表、
リンクされた文字列表、未定義 import、再配置を必要とするため strip-all を
拒否します。NeverC が `-r --strip` を許可するのは、Android ターゲットで
`-fandroid-kernel-driver-mode` と `-r` が有効、かつ出力名が `.ko` で終わる
場合だけです。通常の `-r` と中間 `.o` は引き続き拒否されます。

`neverc make release` は推奨リリースコマンドで、`-O2 --strip` に展開されます。
`.nvk-build-flags` がなければ `make` の既定値は debug で、自動的に release を
選びません。サンプル Makefile は明示的なプロファイル選択を保存するため、以後の
`make push`、`make run`、ターゲットなしの `make` は同じ成果物を使います。
`make debug` または明示的な `PROFILE=...` は保存した選択を更新し、
`make clean` は保存状態を削除して次のビルドを debug に戻します。この最終経路
では NeverC はデバッグセクション、`.comment`、再配置に不要なローカル／未定義
エントリを除去し、`.strtab` を再構築します。

対象となる保持済み定義には、IDA に着想を得つつ予約接頭辞を使わない決定的な
構造名を付けます。

- `STT_FUNC` は `fn_HEX`。
- `STT_OBJECT` は `obj_HEX`。
- 実行可能な `STT_NOTYPE` は `code_HEX`。
- その他の割り当て済み `STT_NOTYPE` は `sym_HEX`。
- `SHN_ABS` は `abs_HEX`。
- `SHF_ALLOC` 外の定義は
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`。

非割り当て形式の 2 フィールドを含むすべての `HEX` フィールドは、不要な
先頭ゼロのない大文字 16 進数です。同じ綴りを必要とする複数のシンボルには、
決定的な 10 進の別名 `_1`、`_2` などを追加します。

この綴りは IDA の表現に着想を得ていますが、dummy-name 名前空間は使いません。
新規 IDA 9.4 データベースで確認すると、ELF のユーザーシンボル `sub_0`、
`sub_4`、`loc_8` は `_sub_0`、`_sub_4`、`_loc_8` と表示される一方、
`fn_0`、`code_8`、`obj_10` はそのまま表示されます。Hex-Rays の
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) 文書も、
`sub_` などの dummy 接頭辞で始まるユーザー名には `_` を付加すると説明して
います。NeverC は IDA に `sub_` を生成させる目的で通常の定義の `st_name` を
空にしません。Android/Linux モジュールの kallsyms は歴史的に名前が空の
エントリを無視し、空名では監査可能な直列化名の契約も失われるためです。
元から空である必要があるエントリとセクションシンボルは正確に保持します。

ELF では複数のシンボルが同じ canonical analysis EA を共有できます。NeverC
は完全な alias 集合を `.symtab` に保持または生成しますが、IDA 9.4 の
アドレス名モデルは同一アドレスのシンボルのうち主名称を 1 つだけ実体化する
ことがあります。そのため、IDA に表示されない alias が ELF から失われたとは
限りません。完全な集合は `llvm-readelf` または `llvm-nm` で監査してください。

割り当て済みシンボルの `HEX` は NeverC canonical analysis EA、つまり静的
解析だけに使う規範実効アドレスです。カーソル 0 から始め、最終セクション
ヘッダー順に、最終的に保持する `SHF_ALLOC` セクションを走査します。
カーソルを `max(sh_addralign, 1)` に整列してそのセクションの基点とし、
`max(sh_size, 1)` 進めます。EA は基点と最終 `st_value` の和です。
`abs_HEX` は最終の絶対 `st_value` を使います。非割り当て形式では
`FINAL_SECTION_ORDINAL_HEX` が最終セクション序数、`OFFSET_HEX` がその
セクション内の最終 `st_value` です。これらの座標はハッシュ値でも暗号化
結果でもなく、ファイルオフセット、ELF 仮想アドレス、カーネル実行時
アドレスでもありません。ローダーと KASLR は実行時にモジュールを別の位置へ
配置できます。

次の名前は正確なまま保持されます。

- モジュールローダーが名前で解決する、すべての `SHN_UNDEF` import。
- `.modinfo`、`.text.ftrace_trampoline`、`.gnu.linkonce.this_module`、
  `__versions`、`.codetag.alloc_tags` 内で定義されたシンボル。
- `init_module`、`cleanup_module`、`__cfi_check`、`__cfi_check_fail`、
  `__cfi_jt_init_module`、`__cfi_jt_cleanup_module`。
- `__typeid__` または `__kcfi_typeid_` で始まる名前。

IDA の `extern` 領域は解析用に合成された表示であり、実在する ELF セクション
ではありません。最終 `ET_REL` `.ko` の外部再配置対象は `.symtab` 内の
`SHN_UNDEF` エントリで、その正確な名前をローダーが必要とします。そのため
方針は実際の ELF シンボルクラスと定義セクションに従います。未定義インポートは
元の名前を保ち、対象となる定義は解析ツール上の分類にかかわらず改名します。

すべての名前は変更前にグローバルに計画します。同じ基底候補を共有する定義には、
決定的な順序で番号なしの形式、`_1`、`_2` などを割り当てます。この通常の
名前割り当てはエラーではありません。生成名が原文どおり保持する名前の予約
名前空間と衝突する場合、または座標／接尾番号の計算が数値範囲を超える場合は
最終処理を中止します。`SHN_COMMON`、`SHN_LIVEPATCH`、未知の ELF 予約
セクションインデックスを検出した場合も、推測せず安全側に倒して拒否します。
ロード可能な最終モジュールでは `SHN_COMMON` は無効なので、`-fno-common` で
コンパイルしてください。
Livepatch モジュールには元のシンボル表の順序とインデックス、および追加の
再配置メタデータが必要であり、この方針はそれらの保持を保証しません。

検出には複数のシグナルを使います。`SHN_LIVEPATCH` シンボル、`.klp.*`
セクション、`SHF_RELA_LIVEPATCH` フラグ、または NUL 区切りの `.modinfo` に
ある `livepatch=` で始まるフィールドのいずれかがあれば livepatch モジュール
として安全側に倒して拒否します。`.klp.*` セクションや livepatch 再配置
フラグがなくても、この `.modinfo` マーカーだけで拒否するには十分です。

置換するのは対象となる `.symtab` 名だけです。ロード可能な `.ko` には
`.symtab`、リンク先の `.strtab`、再配置が引き続き必要なため、汎用ツールが
`not stripped` と表示しても正当です。BTF、モジュール export、`.modinfo`、
`__versions`、trace metadata、`__ksymtab_strings`、`.rodata`、文字列
リテラルなどの独立した格納域／インターフェイスから、元の名前や識別文字列が
漏れる場合があります。通常のカーネルシンボル名は kallsyms と診断でも変わる
ため、シンボルベースの ftrace、kprobe/BPF attach、クラッシュレポートの有用性
が下がります。診断には未ストリップの debug ビルドを使い、release モジュール
で private シンボルの元名に依存しないでください。

`.ko` を `llvm-strip --strip-all` や `objcopy` で後処理せず、codetag/BTF/ABI
セクションを安易に削除しないでください。署名はストリップ後の最終バイト列に
行ってください。署名後の変更は署名を無効にします。`clean` はファイル削除
だけにし、既存モジュールをストリップまたは署名してはいけません。

## セキュリティ上の境界

ストリップは価値の高い名前とデバッグメタデータを消し、解析コストを上げますが、
ネイティブ機械語のリバースエンジニアリングを不可能にはできません。正しく
ストリップしたバイナリにも次が残り得ます。

- ローダーに必要な動的インポート／エクスポート名。
- `.ko` のローダー必須名と `.symtab` 以外に保存された名前。
- 文字列リテラル、リフレクション表、アプリ固有メタデータ。
- アンワインド、再配置、署名、ロード設定の記録。
- 機械語と観測可能な制御フロー。

`--strip` が制御するのは最終イメージだけです。明示的に要求したリンク
マップ、最適化記録、`-save-temps` 出力などは削除しません。リリース
ディレクトリを監査し、これらの付随ファイルを配布しないでください。

必要に応じて文字列暗号化、難読化、改ざん防止を別レイヤーとして使い、秘密に
すべき値をクライアントバイナリへ埋め込まないでください。

## 成果物の検証

CI では LLVM のオブジェクトツールでリリース成果物を検査できます。対象形式に
合わせてコマンドを調整し、必要な ABI 名は明示的に許可してください。
下の否定形の `strings` 検査は一致がないことを期待し、その場合だけ成功
終了します。

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

ロード可能な ELF `ET_REL` `.ko` では `.symtab` を意図的に保持するため、
汎用 `file` ツールが `not stripped` と表示する場合があります。この表示を
release 成否の基準にしないでください。代わりに DWARF と `.comment` がない
こと、対象の定義が正規の大文字 16 進形式 `fn_`/`obj_`/`code_`/`sym_`/
`abs_` であること、`SHN_UNDEF` import と必須のローダー／CFI 名が正確な
ままであること、再配置が有効であることを確認します。名前漏洩が問題なら、
BTF、export、modinfo、versions、trace metadata、文字列も個別に監査します。

ストリップ済み成果物にはソースレベルのデバッグセクションや非公開の静的
シンボル名がないはずです。必要な動的名と実行時メタデータは正常です。
