**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode 與 C++ 翻譯

將純程式碼函式安裝進既有程式時使用 `-fdyncode-hosted`。它要求 `-fdyncode-keep-obj`，在物件中保留編譯器產生的 CFI/SEH，且不將中繼資料放入扁平載荷。原生程式碼節與零偏移入口保持展開記錄的對應關係。

Hosted 模式關閉隱含系統呼叫、PEB、堆 arena 及字串執行階段改寫。可寫/TLS 全域變數與未解析呼叫明確失敗；資料或回呼指標可由參數傳入。獨立 DynCode 保持原有行為。

核心 C++ 翻譯現在接受 `--target`，將 C23、來源映射及清單繫結至該 ABI。`cpp-core-v2` 支援文件所列 C++17 子集，包括模板及正常控制流程的建構與解構，不實作 C++ 例外、RTTI 或虛擬分派，也不保證外部例外傳播時的清理。

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

審計包含 `execution_profile="hosted-v1"`、`keep_unwind=true`、`target_triple`。`input_digest` 對保留物件求雜湊，`output_digest` 對重定位及驗證後的最終載荷求雜湊。安裝器須驗證摘要、程式碼與入口布局，再註冊真實展開記錄。
