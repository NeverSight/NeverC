**Languages**: [English](../hosted-dyncode.md) | [简体中文](hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](../ko/hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode 与 C++ 翻译

将纯代码函数安装进现有程序时使用 `-fdyncode-hosted`。它要求 `-fdyncode-keep-obj`，在对象中保留编译器生成的 CFI/SEH，但不将元数据放进扁平载荷。代码保持为一个原生节，入口偏移为零，从而保留与展开记录的对应关系。

Hosted 模式关闭隐式系统调用、PEB、堆 arena 和字符串运行库改写。可写/TLS 全局变量及未解析的运行库调用会明确失败；数据或回调指针可通过参数传入。独立 DynCode 模式保持原有行为。

核心 C++ 翻译现在接受 `--target`，并将生成的 C23、源映射和清单绑定到该 ABI。`cpp-core-v2` 支持其文档规定的 C++17 子集，包括模板及正常控制流中的构造和析构。它不实现 C++ 异常、RTTI 或虚分派；外部异常传播时的清理不属于该配置。

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

审计记录 `execution_profile="hosted-v1"`、`keep_unwind=true` 和 `target_triple`。`input_digest` 对保留的对象求哈希，`output_digest` 对重定位并验证后的最终载荷求哈希。安装器必须验证两个摘要及最终代码和入口布局的逐字节一致性，再注册真实的编译器展开记录。
