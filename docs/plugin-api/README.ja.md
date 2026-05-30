**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC Out-of-Tree プラグイン API

NeverC は、out-of-tree パスプラグイン向けに**純粋な C ABI** を提供します。プラグインは共有ライブラリ（`.dll` / `.so` / `.dylib`）であり、コンパイルパイプラインの指定ポイントにカスタムパスを登録します。プラグインのコンパイルに必要なのは**ヘッダー 1 つ**（`NevercPluginAPI.h`）のみで、LLVM や CRT への依存は**ゼロ**です。すべての機能はホスト提供の vtable を通じてルーティングされます。

## 1. クイックスタート

### 最小プラグイン

```c
#include "neverc/Plugin/NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### ビルド

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include

make -C /path/to/pluginsdk/examples
```

### 実行

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. アーキテクチャ

**主な特徴：**

- **シングルヘッダー SDK**：プラグインのコンパイルに必要なのは `NevercPluginAPI.h` のみ。
- **ゼロ依存**：LLVM ヘッダー不要、CRT リンク不要。すべての操作は vtable 経由。
- **純粋な C ABI**：C、C++、Zig、Rust（FFI）、またはC リンケージの共有ライブラリを生成できるあらゆる言語で記述可能。
- **バージョンセーフ**：`NEVERC_API_FN(api, Field)` で呼び出し前にオプションの vtable エントリを確認。

## 3. プラグインエントリポイント

すべてのプラグインは以下の関数をエクスポートする必要があります：

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `APIVersion` | `uint32_t` | `NEVERC_PLUGIN_API_VERSION` である必要 |
| `PluginName` | `const char *` | 可読名 |
| `PluginVersion` | `const char *` | セマンティックバージョン |
| `RegisterPasses` | 関数ポインタ | 全パスを登録するために一度呼ばれる |
| `Destroy` | 関数ポインタ | オプションのクリーンアップ、`NULL` 可 |

## 4. パスタイプ

- **Module Pass（IR）**：LLVM IR モジュールを操作。IR の読み取り・変更が可能。
- **Machine Pass（MIR）**：命令選択後のマシンレベル IR を操作。
- **Binary Pass**：生バイト操作（shellcode 抽出、バイナリパッチ）。
- **Linker Pass**：リンク時に動作、シンボルとセクションにアクセス。

## 5. フックポイント

フックはパスがパイプラインの**いつ**実行されるかを決定します。

### 通常フロー

| フック | レベル | 説明 |
|--------|-------|------|
| `NEVERC_HOOK_PRE_OPT` | IR | LLVM 最適化パス前 |
| `NEVERC_HOOK_POST_OPT` | IR | LLVM 最適化パス後 |
| `NEVERC_HOOK_PIPELINE_START` | IR | パイプラインの最初 |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | IR パイプラインの最後 |
| `NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT` | MIR | pre-emit マシンパス前 |
| `NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR` | MIR | 全マシンパス後 |

### Shellcode / LTO / リンカーフロー

Shellcode フックは `NEVERC_HOOK_SC_*`、LTO フックは `NEVERC_HOOK_LTO_*`、リンカーフックは `NEVERC_HOOK_LINK_*` プレフィックスを使用します。

## 6. 不透明ハンドル型

すべての IR/MIR オブジェクトは不透明ハンドル経由でアクセス。ハンドルは受け取った**パスコールバックのスコープ内でのみ有効**です。

| ハンドル | 表現 |
|---------|------|
| `NevercModuleRef` | LLVM Module |
| `NevercValueRef` | LLVM Value（関数、命令、グローバル変数） |
| `NevercBasicBlockRef` / `TypeRef` / `ContextRef` | BasicBlock / Type / Context |
| `NevercBuilderRef` | IR Builder |
| `NevercMetadataRef` / `NamedMDRef` / `ComdatRef` | Metadata / Named Metadata / COMDAT |
| `NevercMachineFuncRef` / `MBBRef` / `MInstrRef` | マシン関数 / ブロック / 命令 |
| `NevercUseRef` | Use-def チェーンエントリ |
| `NevercLinkerSymbolRef` / `SectionRef` | リンカーシンボル / セクション |

## 7. データ構造

ホストは vtable 経由で高性能データ構造を提供：**Arena**（バンプポインタアロケータ）、**StrMap**（文字列キーハッシュマップ）、**IntMap**（整数キーハッシュマップ）、**StrBuilder**（インクリメンタル文字列構築）、**ValueSet**（値ハッシュセット）。

## 8. バージョン互換性

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. プラグイン引数

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. ライフタイムルール

| リソース | ライフタイム | クリーンアップ |
|---------|------------|--------------|
| 不透明ハンドル | パスコールバック内 | 解放不要 |
| `NevercBuilderRef` | `BuilderCreate` で作成 | `BuilderDispose` |
| ヒープ文字列 | 呼び出し元所有 | `Free` |
| Arena 割り当て | Arena 所有 | `ArenaDestroy` |

## 11. ベストプラクティス

1. **Arena ファースト**：一時データには `NEVERC_TRY_ARENA` を使用。
2. **バージョンガード**：`NEVERC_API_FN` で新しい vtable 呼び出しをラップ。
3. **コールバック反復優先**：`ModuleForEachDefinedFunction` はマクロより高速。
4. **CRT 依存なし**：すべての操作は vtable 経由。
5. **クリーンリターン**：パス返却前にすべてのリソースを解放。

## 12. Plugin SDK 内容

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h    # 必要なのはこのヘッダーのみ
└── examples/
    ├── Makefile             # スタンドアロンビルドテンプレート
    ├── ExamplePlugin.c      # 包括的デモ
    ├── CrtShimPlugin.c      # ゼロ CRT 依存の概念実証
    └── BenchPlugin.c        # HostAPI スループットベンチマーク
```

## 13. 関連ドキュメント
