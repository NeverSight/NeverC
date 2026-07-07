**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文档索引](../README.zh-CN.md)

# NeverC 路线图

本文档概述 NeverC 项目在现有 dyncode 编译器和内置运行时之外的主要规划方向。

---

## 1. 标准库 (`std`)

NeverC 将提供一套全面的标准库，参照 Go 标准库设计——提供开箱即用的包，覆盖常见系统编程需求，无需外部依赖。

### 计划中的包


| 包           | 描述                                                       |
| ----------- | -------------------------------------------------------- |
| `fmt`       | 格式化 I/O（printf 系列 + 类型安全扩展）                              |
| `os`        | 操作系统交互：环境变量、进程管理、文件权限                                    |
| `io`        | Reader/Writer 接口、缓冲 I/O、管道工具                             |
| `fs`        | 文件系统操作：遍历、glob、临时文件、原子写入                                 |
| `net`       | TCP/UDP 套接字、DNS 解析、HTTP 客户端/服务端                          |
| `net/http`  | HTTP/1.1 和 HTTP/2 客户端与服务端                                |
| `crypto`    | 哈希（SHA-256、SHA-512、BLAKE3）、HMAC、AES、ChaCha20、RSA、Ed25519 |
| `encoding`  | JSON、Base64、Hex、CSV、二进制（大小端）                             |
| `sync`      | 互斥锁、读写锁、WaitGroup、Once、原子操作                              |
| `time`      | 单调/墙钟时间、时长、定时器、格式化                                       |
| `bytes`     | 字节切片操作、缓冲区                                               |
| `math`      | 数学常量、基本函数、随机数生成                                          |
| `sort`      | 泛型排序与搜索                                                  |
| `container` | 链表、堆、环形缓冲区                                               |
| `log`       | 带级别的结构化日志                                                |
| `flag`      | 命令行参数解析                                                  |
| `path`      | 路径操作（POSIX 和 Windows）                                    |
| `regexp`    | 正则表达式匹配（RE2 语法）                                          |
| `compress`  | gzip、zlib、zstd、lz4                                       |
| `hash`      | CRC32、CRC64、FNV、xxHash                                   |
| `unicode`   | Unicode 表、大小写折叠、UTF-8/UTF-16 转换                          |


### 设计原则

- **纯 C23** — 每个包都以标准 NeverC/C23 编译；无隐藏 C++ 或平台特定汇编
- **零外部依赖** — 标准库以 LLVM bitcode 嵌入编译器，与现有的 `string` 和 `mimalloc` 内置功能一致
- **跨平台** — 所有包在 macOS、Linux、Windows（x86_64 / AArch64）上工作
- **DynCode 兼容** — 在独立模式下有意义的包（如 `crypto`、`encoding`、`bytes`）支持 `-fdyncode`

---

## 2. 混淆插件套件 (`neverc-obfuscation`)

NeverC 将提供第一方代码混淆插件套件——既是 Plugin API 完整能力的参考实现，也是开箱即用的生产级代码保护工具。

### 计划中的插件

| 插件 | 钩子点 | 描述 |
|------|--------|------|
| 垃圾代码插入 | `RunAfterFinalMIR` | 在真实基本块之间插入语义无效但语法合法的指令序列 |
| 不透明谓词 | `RunBeforePreEmit` | 插入由数论不变量守护的恒真/恒假分支；增加混淆分析的死路径 |
| 控制流平坦化 | `RunAfterStackify` | 将基本块打散到 switch 分发循环中；破坏反编译器可识别的自然 CFG 结构 |
| 反篡改 | `RunPostFinalize` | 嵌入自完整性检查（代码段的 CRC/哈希），修补时触发失败 |
| 多态引擎 | `RunPostExtract` | 基于种子的输出变化——每次编译产生功能等价但结构不同的代码；对抗签名检测 |
| MBA（混合布尔算术） | `RunAfterInlining` | 用等价但不透明的 MBA 形式替换算术/布尔表达式（如 `x + y` → `(x ^ y) + 2 * (x & y)` 链）；抗符号执行 |
| VM（代码虚拟化） | `RunAfterFinalIR` | 将函数转换为自定义字节码，由内嵌解释器执行；对抗静态反汇编和签名匹配 |

### 设计原则

- **纯 Plugin API** — 每个混淆功能以 `.dll` / `.so` / `.dylib` 插件形式提供；无需分叉编译器
- **可组合** — 插件可叠加：先 MBA，再平坦化，再虚拟化——每个 pass 相互独立
- **可配置** — 逐函数注解（`__attribute__((obfuscate("vm")))`）选择性保护热点路径，避免全程序开销
- **可审计** — 每个插件记录其变换以供安全审查；通过 `-fdyncode-dump-ir` 可查看变换前后 IR 差异
- **DynCode 兼容** — 所有插件在 `-fdyncode` 模式下工作；生成的代码保持位置无关

---

## 3. UI 组件库 (`neverc-ui`)

NeverC 将提供类似 Qt 的跨平台 UI 组件库——但采用 HTML/JS/CSS 前端渲染引擎，天然适合 AI 生成界面。

### 目标

- **组件化架构** — 窗口、按钮、文本输入、列表、树、表格、菜单、对话框、选项卡和布局容器作为一等 C 类型
- **HTML/JS/CSS 渲染器** — 通过内嵌轻量级浏览器引擎渲染 UI；开发者编写 C 逻辑，视觉层使用标准 Web 技术
- **拖拽式可视化设计器** — 配套 GUI 构建器，生成 NeverC 兼容的 C 代码，无需手写布局代码即可快速原型设计
- **AI 原生设计流程** — LLM 可在一轮生成 C 业务逻辑和 HTML/CSS 布局，因为视觉层使用的是地球上最广泛理解的 UI 语言
- **原生外观** — 通过 CSS 变量和系统字体/颜色检测实现平台自适应主题（macOS、Windows、Linux）
- **轻量级嵌入** — 渲染器作为内置运行时提供（类似 `string` / `mimalloc`）；没有 Electron 级别的开销
- **事件系统** — 用户交互的 C 回调函数（点击、输入、调整大小、拖拽、键盘、自定义事件）
- **数据绑定** — C 结构体与 UI 状态之间的声明式绑定；变更自动传播
- **自定义渲染** — 通过原始 canvas/WebGL 进行游戏 UI、数据可视化或自定义控件的逃逸口

### 为什么用 HTML/CSS 做 C 的 UI 库？

- 每个 AI 模型都已经掌握 HTML/CSS——生成 UI 代码无需专门训练
- Web 技术是经过最充分验证的布局系统；无需重新发明 flexbox、grid 或文字渲染
- 安全研究工具（仪表板、十六进制查看器、数据包检查器）受益于丰富的样式界面，无需学习专有控件 API
- 可视化设计器导出的 HTML 模板既可在 NeverC 应用中使用，也可在独立浏览器中快速迭代

---

## 4. IDE 与语言工具 (`neverc-ide`)

NeverC 将为 `.nc` 语言扩展提供一流的 IDE 支持——VSCode 扩展实现即时生产力，独立 NeverC IDE 提供完全集成的开发体验。

### VSCode 扩展

- **语法高亮** — 完整 `.nc` 语法，支持 NeverC 特有类型的语义 token（`string`、`u8`–`u64`、`i8`–`i64`、`f32`、`f64`）
- **智能补全** — 内置类型、点调用方法（`.c_str()`、`.len()`、`.starts_with()`）和 `#include` 路径的自动补全
- **诊断** — 实时显示 `neverc` 编译器的错误和警告
- **跳转到定义** — 跨翻译单元跳转到函数、结构体和宏定义
- **悬停文档** — 内置函数、编译器内建和标准库包的内联文档
- **代码操作** — 常见错误的快速修复建议，`std` 包的自动导入
- **调试** — 集成 LLDB/GDB 调试适配器，支持断点、单步和变量检查
- **DynCode 模式** — 针对 `-fdyncode` 管线的语法感知功能：坏字节高亮、dyncode 大小显示、目标特定补全
- **插件 API 集成** — 插件钩子点可视化和脚手架

### 独立 IDE

- **基于 NeverC UI (`neverc-ui`)** — IDE 本身是 HTML/JS/CSS 组件库的展示，用自己的 UI 框架构建
- **集成终端** — 无需离开 IDE 即可构建、运行和调试
- **可视化 dyncode 管线** — IR → MIR → 提取管线的图形视图，逐 pass 输出检查
- **项目模板** — 一键脚手架：宿主二进制、dyncode、EVM 合约、Solana 程序
- **AI 辅助编码** — 内置 LLM 集成，理解 NeverC 语义，生成 `.nc` 代码，解释编译器诊断
- **跨编译仪表板** — 可视化目标选择器，平台矩阵和构建状态

### 为什么同时做 VSCode 和独立 IDE？

- VSCode 覆盖了大多数已经在该生态中的开发者
- 独立 IDE 为安全研究员提供更深入的、专门构建的体验，包含 dyncode 管线可视化和集成二进制分析
- 两者共享同一个语言服务器后端——改进同时惠及两者

---

## 5. EVM 智能合约后端

NeverC 将支持把 C 源代码编译为 EVM（以太坊虚拟机）字节码——使开发者能用 C 代替 Solidity 编写智能合约。

### 目标

- **新 LLVM 后端目标** — `evm` 目标三元组（如 `neverc --target=evm hello.c -o contract.bin`）
- **ABI 兼容** — 生成 Solidity 兼容的 ABI 描述符，合约可与现有以太坊工具链（Hardhat、Foundry、ethers.js）交互
- **存储布局** — 将 C 结构体映射到 EVM 存储槽，布局确定性
- **内置 EVM 原语** — `msg.sender`、`msg.value`、`block.number`、`tx.origin` 作为内置变量或内建函数
- **payable / view / pure 修饰符** — 映射到 Solidity 可见性语义的函数属性
- **事件发射** — 从标注的函数调用生成 `LOG0`–`LOG4` 操作码
- **Gas 优化** — IR pass 最小化 gas 开销（栈调度、常量折叠、死存储消除）
- **revert / require** — 带自定义错误消息的错误处理原语

### 为什么用 C 写 EVM？

- Solidity 的语法对 JavaScript 开发者友好，但对系统程序员陌生；C 是通用语言
- NeverC 现有的 IR 优化管线在很多场景下能生成比 `solc` 更紧凑的字节码
- 安全研究员已经用 C 思考——用 C 编写审计工具和 fuzzer 对 C 合约是天然匹配
- 插件 API 允许在编译期进行自定义 gas 分析和漏洞检测 pass

---

## 6. Solana eBPF 后端

NeverC 将支持把 C 源代码编译为 Solana 的 eBPF 字节码——实现用 C 开发链上程序。

### 目标

- **eBPF 目标** — `sbf`（Solana BPF）目标三元组（如 `neverc --target=sbf-solana hello.c -o program.so`）
- **Solana 运行时绑定** — 内置 Solana 系统调用头文件：`sol_invoke_signed`、`sol_log`、`sol_memcpy`、账户信息结构体
- **账户模型** — C 结构体覆盖 Solana 账户数据，自动序列化/反序列化
- **CPI（跨程序调用）** — 类型安全的包装器，用于调用其他链上程序
- **PDA（程序派生地址）** — 内置 PDA 推导和验证函数
- **计算预算感知** — 当估计的计算单元超出程序限制时发出编译器警告
- **Anchor 兼容** — 可选 IDL 生成，与 Anchor 前端互操作

### 为什么用 C 写 Solana？

- Solana 运行时本身执行 eBPF——C 是 BPF 目标最自然的源语言
- 现有基于 C 的 BPF 工具链（clang + solana-bpf）配置复杂；NeverC 将一切打包到单一二进制
- 性能关键的程序受益于 C 的零开销抽象和 NeverC 的优化 pass
- dyncode 编译经验（位置无关、最小运行时代码）直接映射到链上程序约束

---

## 时间线

这些功能目前处于研究和设计阶段。暂不承诺具体发布日期。进展将在本文档中更新，并在项目发布页公布。

| 功能 | 状态 |
|------|------|
| 标准库 (`std`) | 研究 / 设计 |
| 混淆插件套件 (`neverc-obfuscation`) | 研究 / 设计 |
| UI 组件库 (`neverc-ui`) | 研究 / 设计 |
| IDE 与语言工具 (`neverc-ide`) | 研究 / 设计 |
| EVM 智能合约后端 | 研究 / 设计 |
| Solana eBPF 后端 | 研究 / 设计 |


