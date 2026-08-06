**語言**: [English](python.md) | [简体中文](python.zh-CN.md) | [繁體中文](python.zh-TW.md) | [日本語](python.ja.md) | [한국어](python.ko.md) | [Français](python.fr.md) | [Deutsch](python.de.md) | [Español](python.es.md) | [Italiano](python.it.md) | [Русский](python.ru.md) | [العربية](python.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# Python 外掛

NeverC 可透過原生外掛所使用的同一個 `-fplugin=` 選項載入 Python
原始檔。一般原始碼建置現在預設啟用 Python 外掛，並在安裝時捆綁執行環境：

```sh
cmake -S llvm -B build -C neverc/cmake/caches/NeverC.cmake \
  -DCMAKE_INSTALL_PREFIX="$PWD/neverc-install"
cmake --build build --target install
```

全新建置預設採用 `NEVERC_ENABLE_PYTHON_PLUGINS=ON` 與
`NEVERC_BUNDLE_PYTHON_RUNTIME=ON`。設定階段需要 CPython 3.10 或更新版本，
包括嵌入用開發標頭與共享函式庫；安裝時會自動把所選解譯器的確切版本複製到相鄰
的 `python/` 目錄。建置樹中的原始執行檔仍可能使用建置期 Python，但安裝後的
編譯器在執行時不需要外部 Python、`PYTHONHOME` 或 `PYTHONPATH`。NeverC 官方
封存檔固定選用並捆綁 CPython 3.12.10。

Linux 安裝階段要求 `PATH` 中有 `patchelf`。當 `CMAKE_CROSSCOMPILING` 時會拒絕
自動捆綁，因為不能把主機解譯器放入目標架構編譯器套件；此時應關閉捆綁並明確
封裝目標執行環境。若要刻意建置完全不含 Python 的編譯器，請同時傳入
`-DNEVERC_ENABLE_PYTHON_PLUGINS=OFF` 與
`-DNEVERC_BUNDLE_PYTHON_RUNTIME=OFF`。

可用 `python3 -m pip install ./pluginsdk/python` 安裝開發套件，也可將該目錄加入
`PYTHONPATH`，或建置/安裝 `neverc-pluginsdk` 元件。NeverC 也會自動尋找
`<neverc 所在目錄>/../pluginsdk/python` 中已暫存的 SDK。

## 最小外掛

```python
from neverc_plugin import Plugin


@Plugin(id="com.example.minimal", name="Minimal Python Plugin", version="1.0.0")
class MinimalPlugin:
    def on_process_begin(self, ctx):
        ctx.state = {"sessions": 0}
```

以檔案系統路徑載入：

```sh
neverc -fplugin=/absolute/path/to/minimal.py -fsyntax-only input.c
```

decorator 接受一個規範外掛 ID、非空白顯示名稱與嚴格語意版本。一個 script
只宣告一個外掛 class。不同 script 是彼此獨立的 module，也可與原生外掛混用。

## 生命週期

所有 hook 都是選用的：

- `on_process_begin(ctx)` 與 `on_destroy(ctx)` 包住編譯器 process。
- `register(ctx)` 在 phase graph 凍結前註冊選項與 observer。
- `on_session_begin(ctx)` 與 `on_session_end(ctx)` 包住一次 invocation。
- `on_task_begin(ctx)` 與 `on_task_end(ctx)` 包住一個編譯工作單元。

begin hook 可回傳 Python 值或指派 `ctx.state`；配對的 end hook 可讀取該值。
其他 hook 與 observer callback 必須回傳 `None`。v1 Python 外掛採
session-serial 且不可重入。

## 選項與 observer

```python
from neverc_plugin import Plugin
from neverc_plugin.domains import driver


@Plugin(id="com.example.trace", name="Trace", version="1.0.0")
class TracePlugin:
    def register(self, ctx):
        ctx.option(
            "--trace-python",
            kind="flag",
            value_type="bool",
            help="Trace raw driver arguments",
        )
        ctx.observer(
            driver.RAW_ARGUMENTS,
            when=("before", "after"),
            fn=self.observe,
        )

    def observe(self, frame):
        if frame.option_values("--trace-python"):
            frame.check_cancelled()
            frame.emit_remark(f"arguments: {frame.arguments}", code=1001)
```

`neverc_plugin.phases` 包含由規範 phase schema 產生的全部 130 個內建 phase
常數。Observer frame 提供 phase 與 route 資料、不透明的輸入/輸出 handle、
已解析的外掛選項值、diagnostic、取消檢查，以及 `driver.RAW_ARGUMENTS` 的原始
參數。原生 context 與 frame handle 會檢查生命週期：callback 結束後使用保留
物件會引發 `RuntimeError`。

選項 kind 為 `flag`、`joined`、`separate`、`multi_arg`；value type 為
`bool`、`int`、`uint`、`string`、`enum`、`path`；multiplicity 為
`single`、`last_wins`、`append`。enum 選項傳入
`enum_values={名稱: 整數}` mapping。`argument_count` 僅適用於 `multi_arg`。

## 錯誤、安全性與目前範圍

未捕捉的 Python exception 會轉換成 `NEVERC_STATUS_PLUGIN_EXCEPTION`。在活動
session/task callback 中，NeverC 會把格式化 traceback 輸出成結構化外掛
diagnostic；import 與 activation 失敗則會在 loader error 中包含 traceback。
嵌入式 interpreter 在 process 範圍共享，NeverC 刻意不 finalize；每個外掛的
物件仍會在 unload 時釋放。

Python 外掛是受信任的編譯器擴充。它們在 process 內執行，可 import 任意
module，並擁有與 NeverC 相同的檔案系統與 process 權限；沒有 sandbox。

v1 除了選項註冊外刻意維持唯讀。它不公開 interceptor、provider、artifact
修改、各 domain 專用的 IR/MIR/Link 物件模型、subinterpreter、manifest 或
module/factory entry point。這些能力需要能強制生命週期的 transaction 與
continuation wrapper；需要時仍可使用原生 C ABI。
