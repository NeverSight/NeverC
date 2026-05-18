**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode コンパイラ](../README.ja.md)

# Shellcode プラグインインターフェース（Plugin SDK）

NeverC の shellcode パイプラインは**コアパイプライン + プラガブルユーザー層**の二重構造。難読化、アンチ逆アセンブリ、EDR 回避、段階エンコーダ等の戦略層機能は**意図的に組み込まない**。

## 1. Finalize パイプライン
`finalizeShellcodeBytes` が順次処理：PostExtract フック → Bad-byte リライタチェーン → キャラセットエンコーダ → Bad-byte 監査 → サイズ制約 → PostFinalize フック。

## 2. Bad-byte リライタ
`registerBadByteRewriteStrategy` で戦略を登録。冪等・確定的・バイトストリームのみ参照。

## 3. キャラセットエンコーダ
`registerCharsetEncoder` で `(Name, Encode, Stub, IsCharsetMember)` タプルを登録。出力はキャラセット検証に合格必須。

## 4. サイズ / アライメント / パディング
`-fshellcode-max-length=`、`-fshellcode-align=`、`-fshellcode-pad=`。プラグイン不要。

## 5. 三層フックマッピング
IR 層 6 フック、MIR 層 3 フック、バイトストリーム層 2 フック。

## 6. 登録位置選択 + PIC カバレッジマトリクス
早い登録 = 広い組込み PIC 修正カバレッジ。推奨：文字列暗号化は `RunAfterPrep`、CFF は `RunAfterInlining`、命令置換は `RunAfterPreEmit`、全ペイロード暗号化は `RunPostFinalize`。複数ライブラリ共存は get/modify/set パターン。
