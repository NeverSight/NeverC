**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← ドキュメント索引](../README.ja.md) · [← NeverC プロジェクト](../i18n/README.ja.md)

# Windows 上の VBS エンクレーブ DLL

NeverC は、64 ビット Windows ターゲット向けに Microsoft 互換の VBS エンクレーブ DLL をリンクできます。サポートされるリンカー契約は次のとおりです。

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

Microsoft リンカーのオプションは、Windows ドライバーの `-Xmslink` または `-Wl,` を使って渡します。

```powershell
neverc.exe --target=x86_64-pc-windows-msvc -fno-lto -shared -nostdlib `
  enclave.obj guarded.obj legacy.obj `
  -lvertdll -lbcrypt -llibcmt -llibvcruntime -lucrt `
  -Xmslink /INCREMENTAL:NO `
  -Xmslink /NODEFAULTLIB `
  -Xmslink /ENCLAVE `
  -Xmslink /INTEGRITYCHECK `
  -Xmslink /GUARD:MIXED `
  -Xmslink /DYNAMICBASE `
  -Xmslink /MACHINE:X64 `
  -o game-security-enclave.dll
```

この例では、`-l` を使って MSVC CRT および UCRT ライブラリのエンクレーブ版を明示的に選択しています。明示的な `-vctoolsdir` または `-winsysroot` の指定は、通常どおり優先されます。これらのオーバーライドがない場合、macOS、Linux、Windows のいずれの `/ENCLAVE` リンクでも、Windows ライブラリは NeverC に同梱されたターゲットランタイムからのみ解決されます。ホストにインストール済みの Visual Studio ツールセットや Windows SDK を自動検出したり、そこへフォールバックしたりすることはありません。

## 同梱ランタイムを使ったクロスホストビルド

コンパイルと COFF リンクはホストに依存しません。ターゲットランタイムをインストールすれば、同じコマンドを macOS、Linux、Windows のいずれでも実行できます。

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

ターゲットパッケージには、Windows ヘッダー、エンクレーブ CRT、エンクレーブ UCRT、`vertdll.lib`、`bcrypt.lib`、およびその他の必要な Windows インポートライブラリが含まれます。同梱ランタイムから解決する場合、明示的な `/ENCLAVE` とグローバルな `/NODEFAULTLIB` を組み合わせたときに限り、NeverC は通常の同梱 CRT/UCRT ディレクトリからエンクレーブ CRT/UCRT ディレクトリへ切り替えます。このモードでは、リンク前に、同梱された `libcmt.lib`、`libvcruntime.lib`、`ucrt.lib`、`vertdll.lib`、`bcrypt.lib` がすべて存在することをドライバーが検証します。ライブラリは引き続き `-l...` で明示的に選択します。`/ENCLAVE` だけではエンクレーブ CRT/UCRT ディレクトリを有効にせず、そこにあるライブラリも選択しません。通常の同梱ランタイム検索パスが引き続き使用されます。

クロスホストのリンク段階では、未署名かつ未処理のエンクレーブ DLL が生成されます。VEIID 処理、SignTool 署名、および `CreateEnclave`/`LoadEnclaveImage` による実際の読み込みは、引き続き Windows でのみ実行できます。そのため、macOS または Linux でリンクした DLL は、最後の 3 段階を実行する Windows のパッケージングマシンまたはテストマシンへ移動してください。ランタイムのインストールと検出については、[ターゲットランタイム](../runtime/README.ja.md)を参照してください。

## 必須のイメージ入力

エンクレーブのリンクでは、次の 2 つのイメージデータ定義を指定する必要があります。

- `__enclave_config`：イメージの `IMAGE_ENCLAVE_CONFIG` データを格納します。
- `_load_config_used`：`EnclaveConfigurationPointer` を格納できるだけの大きさを持つ load-config 構造体です。

NeverC は、未使用コードの除去後も `__enclave_config` を保持し、必要に応じてアーカイブから抽出し、最終的に再配置された load-config ポインターがその構成オブジェクトの仮想アドレスと等しいことを検証します。定義が欠落している、絶対シンボルである、破棄されている、切り詰められている、または誤って再配置されている場合はリンクエラーになります。

`/GUARD:MIXED` は、保護されたオブジェクトファイルと従来のオブジェクトファイルが混在する入力に対して CFG 出力を有効にします。4 バイトの RVA と 1 バイトのメタデータから成る 5 バイトの GFID および GIAT エントリを出力します。現在の通常のターゲットではメタデータはゼロです。`GuardFlags` には CFG およびエントリサイズのビットが設定されます。従来のオブジェクトは、アンワインドメタデータを除外しながら再配置を保守的に走査して、アドレス取得対象を提供します。

明示的なインクリメンタルリンク要求は `/ENCLAVE` と互換性がないため拒否されます。オブジェクトファイルのディレクティブに由来するものも含め、最後に有効となる `/INCREMENTAL` オプションが使用されます。

`/ENCLAVE` が DLL 出力、CFG、整合性チェック、エンクレーブ CRT ライブラリ、VEIID 処理、または署名を暗黙に選択することはありません。ビルドパイプラインでは、これらを明示的に指定してください。同梱ランタイムモードでは、前述のエンクレーブ CRT/UCRT 検索パスと 5 ライブラリの検証は、グローバルな `/NODEFAULTLIB` を明示した場合にのみ有効になります。このオプションがなければ、通常の同梱 Windows ランタイムパスが引き続き使用されます。明示的なユーザーツールチェーンのオーバーライドは、通常どおり優先されます。

## ビルドとデプロイの流れ

1. セキュリティ上重要なソースは、たとえば `-fms-guard=cf` を使い、CFG を有効にしてコンパイルします。最終リンクで `/GUARD:MIXED` を使用する場合、従来のオブジェクトは未計装のままでも構いません。
2. エンクレーブ構成とエントリーポイントを定義し、エンクレーブ CRT/UCRT および必要な Vertdll と BCrypt のインポートライブラリとリンクします。
3. 未署名の PE イメージを検査し、load-config ディレクトリ、CFG テーブル、エンクレーブ構成ポインター、およびベース再配置を検証します。
4. Windows 上で、完成したイメージに Windows SDK の VEIID ツールを実行します。
5. Windows 上で SignTool を使って VEIID 処理済みイメージに署名します。署名は、ファイルに対する最後の変更でなければなりません。
6. Windows ホストでは `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)` を確認し、`CreateEnclave` でエンクレーブを割り当て、`LoadEnclaveImage` で DLL を読み込み、`InitializeEnclave` を呼び出します。

アンチチートシステムでは、通常のゲームプロセスとの間により強い境界が必要なコードや非公開状態を持つ、小規模な検証コンポーネントや鍵処理コンポーネントにエンクレーブが適しています。エンクレーブのインターフェイスは狭く保ち、ホストから渡されるすべてのデータを検証してください。ホストは依然として入力、スケジューリング、ストレージ、可用性を制御します。VBS エンクレーブは、サーバー側の権威、テレメトリ、ドライバーによる防御、通常のプロセス強化を補完するものであり、それらを置き換えるものではありません。

## 検証

`VBS enclave differential CI` ワークフローは Windows 上で実行されます。静的ゲートでは次を行います。

- NeverC リンカーと対象を絞った COFF テストをビルドする。
- Microsoft でリンクしたものと NeverC でリンクしたものに相当するエンクレーブ DLL を作成する。
- 公開されている PE/load-config/CFG の意味論を比較する。
- PE 検証ツールに対してミューテーションテストを実行する。
- 差分ランタイムプローブ用に VEIID 処理済みイメージを準備する。

ランタイムプローブは Microsoft イメージを最初に実行します。ホステッド runner に VBS または利用可能な署名環境がない場合、結果は環境によるスキップとして明示されます。Microsoft 参照イメージが正常に読み込まれた後は、いずれかの NeverC 候補が失敗するとテストは必ず失敗します。構成済みのセルフホステッド VBS runner では、ランタイムの成功を必須ゲートにできます。

リンカーは x86-64 および ARM64 の COFF エンクレーブイメージをサポートします。公開された構成ポインターを検証した後、最終的な通常 DLL のインポート集合から連続した 80 バイトの `IMAGE_ENCLAVE_IMPORT` エントリを生成します。エントリは最初はインポート名のみを保持し、識別フィールドは VEIID がバインドするためゼロです。リンカーは件数、リスト、エントリサイズを書き戻します。アクティブな遅延ロードインポートは拒否されます。`IMAGE_ENCLAVE_CONFIG` 内のバージョン管理されたフィールドに追加のポリシーは課しません。
