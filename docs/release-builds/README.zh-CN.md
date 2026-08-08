**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md) · [← NeverC 项目](../../README.md)

# 发布二进制与 `--strip`

生成用于分发的可执行文件、共享库或最终 Android 内核模块时，请使用
`--strip`。它的短别名是 `-s`，两种写法的行为完全相同。

## 快速开始

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC 在集成链接器内部完成剥离，不会启动外部 `llvm-strip`，因此同一
命令可用于交叉目标 ELF、Mach-O 与 PE/COFF 输出。

请勿将此命令行选项与 CMake 打包开关 `NEVERC_STRIP_BINARY` 混淆：后者
只会在构建后处理 `neverc` 编译器可执行文件，并且可能调用外部 strip
工具；它不会影响 NeverC 编译出的程序。

## 调试信息与符号策略

| 调用方式 | 源码级调试信息 | 普通静态符号名 | Darwin `.dSYM` |
|----------|----------------|----------------|----------------|
| 默认（无 `-g`） | 不生成 | 可能保留；具体默认值取决于格式 | 不生成 |
| `-g` | 生成 | 保留 | 普通 Darwin 链接会生成 |
| `--strip` | 如存在则删除 | 删除非运行时名称 | 不生成 |
| `-g --strip` | 剥离策略优先；交付映像中不存在 | 删除非运行时名称 | 禁止生成 |

未指定 `-g` 时，前端从“不生成源码级调试信息”开始。这**不等于**产物已
完全剥离：ELF 与 Mach-O 仍可能携带普通符号名；PE 通常没有静态 COFF
符号表，除非调试设置要求生成。Auto-LTO 可能丢弃一部分局部名称，但这
不是 strip-all 保证。

`-g` 将策略从“无源码调试信息”切换为“生成源码级调试信息”，并不是在
默认已有调试信息上生成“更多”。ELF/Mach-O 的 `.eh_frame` 或 PE 的
`.pdata`/`.xdata` 等展开数据属于运行时元数据，不是源码级 DWARF，剥离后
仍可能保留。

## 实现与格式行为

驱动把 `--strip` 转换为一个强类型链接策略并传给三个后端。各后端在仍
理解目标格式的阶段应用策略，同时保留加载器或动态 ABI 必需的名称和记录。

| 格式 | 删除内容 | 必要时保留 |
|------|----------|------------|
| ELF | `.debug*` 数据以及普通静态符号表/字符串表 | 动态导入导出、重定位与加载器元数据、展开信息 |
| Android 内核 `.ko`（ELF ET_REL） | `.debug*`、`.comment`，以及未被保留重定位使用的局部/未定义符号 | 一个链接到 `.strtab` 的 `.symtab`、全部重定位及其目标、已定义全局符号、导入、`__versions`、`.codetag.alloc_tags`、模块 ABI 数据 |
| Mach-O | 调试映射/STABS、非运行时局部与全局符号项，以及伴随 `.dSYM` 的生成 | 绑定/导入数据、导出 ABI 名、export trie 项、运行时引用符号 |
| PE/COFF | 嵌入式 DWARF 段，以及存在时的静态 COFF 符号表/字符串表 | PE 导入导出、展开表、加载配置及其他加载器元数据 |

## 作用域与优先级

- `--strip` 支持最终链接的可执行文件、共享库，以及下述严格限定的最终
  Android `.ko` 例外。
- 与 `-c`、普通 `-r`、Android 中间 `.o`、`--emit-static-lib` 或
  `-fdyncode` 组合时，NeverC 会明确报错，而不是静默生成未剥离的非最终
  产物。
- 剥离策略优先于 `-g` 和后端调试开关。
- NeverC 的默认 Auto-LTO 流水线与 `-fno-lto` 均有覆盖。
- 共享库的导入和导出名称在删除会破坏动态 ABI 时必须保留。

## Android 内核模块

Android 模块虽是最终交付物，但仍是 ELF `ET_REL`。Linux 模块加载器需要
符号表、关联字符串表、未定义导入与重定位，因此会拒绝 strip-all 结果。
NeverC 只在以下条件全部满足时允许 `-r --strip`：目标平台是 Android；
启用了 `-fandroid-kernel-driver-mode` 与 `-r`；输出名以 `.ko` 结尾。

该路径的 `--strip` 实现的是 `llvm-strip --strip-unneeded` 的安全边界，而
不是 `--strip-all`：删除调试段、`.comment` 以及未被保留重定位使用的局部或
未定义符号，并重建 `.strtab`，确保已删名称不会以废弃字节残留。它会保留
恰好一个链接到 `.strtab` 的 `.symtab`、所有重定位与必需目标、已定义非局部
符号、导入、`__versions`、`.codetag.alloc_tags`、
`.gnu.linkonce.this_module` 及其他模块加载元数据。

不要再对 `.ko` 执行 `llvm-strip --strip-all`，也不要盲目删除
`.codetag.alloc_tags` 或 `__codetag_*`；它们可能属于加载器/运行时 ABI。
如需模块签名，必须先剥离，再对最终字节签名，因为签名后的任何修改都会使
签名失效。`clean` 目标只能删除文件，绝不能剥离或签名现有模块。

## 安全边界

剥离会删除价值较高的命名与调试元数据，从而提高分析成本，但它**不是**
混淆，也无法让原生机器码变得不可逆向。正确剥离的二进制仍可能包含：

- 加载器必需的动态导入与导出名称；
- `.ko` 中被保留重定位所必需的符号名；
- 字符串字面量、反射表或应用自定义元数据；
- 展开、重定位、签名和加载配置记录；
- 机器码及其可观察控制流。

`--strip` 只约束最终映像，不会删除显式请求的链接映射、优化记录或
`-save-temps` 输出等独立产物；请审计发布目录，不要分发这些旁路文件。

需要时请把字符串加密、混淆和反篡改作为独立防护层；不得在客户端二进制
中嵌入必须保密的秘密。

## 验证产物

可在 CI 中使用 LLVM 目标文件工具检查发布产物。请按目标格式调整命令，
并显式允许程序所需的 ABI 名称。

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM

llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

剥离后的产物不应包含源码级调试段或私有静态符号名。必需的动态名称和
运行时元数据属于预期内容，不应被判定为剥离失败。
