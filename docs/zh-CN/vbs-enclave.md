**语言**: [English](../vbs-enclave.md) | [简体中文](vbs-enclave.md) | [繁體中文](../zh-TW/vbs-enclave.md) | [日本語](../ja/vbs-enclave.md) | [한국어](../ko/vbs-enclave.md) | [Français](../fr/vbs-enclave.md) | [Deutsch](../de/vbs-enclave.md) | [Español](../es/vbs-enclave.md) | [Italiano](../it/vbs-enclave.md) | [Русский](../ru/vbs-enclave.md) | [العربية](../ar/vbs-enclave.md)

[← 文档索引](README.md) · [← NeverC 项目](project.md)

# Windows 上的 VBS enclave DLL

NeverC 可以为 64 位 Windows 目标链接与 Microsoft 兼容的 VBS enclave DLL。支持的链接器契约为：

```text
/DLL /INCREMENTAL:NO /INTEGRITYCHECK /ENCLAVE /GUARD:MIXED
```

通过 Windows 驱动程序的 `-Xmslink` 或 `-Wl,` 传递 Microsoft 链接器选项：

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

本示例通过 `-l` 显式选择 MSVC CRT 和 UCRT 库的 enclave 版本。任一显式指定的 `-vctoolsdir` 或 `-winsysroot` 仍按通常优先级生效。未指定这些覆盖项时，无论在 macOS、Linux 还是 Windows 上，所有 `/ENCLAVE` 链接都只从 NeverC 捆绑的目标运行时解析 Windows 库；驱动程序不会自动探测或回退到宿主上安装的 Visual Studio 工具集或 Windows SDK。

## 使用捆绑运行时进行跨主机构建

编译和 COFF 链接与宿主平台无关。安装目标运行时后，同一条命令可以在 macOS、Linux 或 Windows 上运行：

```text
neverc runtime install windows-x64
neverc runtime install windows-arm64
```

目标软件包包含 Windows 头文件、enclave CRT、enclave UCRT、`vertdll.lib`、`bcrypt.lib` 和其他所需的 Windows 导入库。使用捆绑运行时解析时，只有显式 `/ENCLAVE` 与全局 `/NODEFAULTLIB` 同时出现，NeverC 才会从普通的捆绑 CRT/UCRT 目录切换到 enclave CRT/UCRT 目录。在此模式下，驱动程序会在链接前验证捆绑的 `libcmt.lib`、`libvcruntime.lib`、`ucrt.lib`、`vertdll.lib` 和 `bcrypt.lib` 均存在。这些库仍需使用 `-l...` 显式选择。单独使用 `/ENCLAVE` 既不会启用 enclave CRT/UCRT 目录，也不会选择其中的库；它继续使用捆绑的普通运行时搜索路径。

跨主机链接阶段生成未签名、未经处理的 enclave DLL。VEIID 处理、SignTool 签名以及通过 `CreateEnclave`/`LoadEnclaveImage` 实际加载仍只能在 Windows 上进行，因此请将 macOS 或 Linux 上链接的 DLL 移至 Windows 打包机或测试机，以完成最后三个阶段。有关运行时的安装和发现，请参阅[目标运行时](runtime.md)。

## 必需的映像输入

enclave 链接必须提供以下两个映像数据定义：

- `__enclave_config`，其中包含映像的 `IMAGE_ENCLAVE_CONFIG` 数据。
- `_load_config_used`，其 load-config 结构必须足够大，能够包含 `EnclaveConfigurationPointer`。

NeverC 会在死代码剥离期间保留 `__enclave_config`，必要时从归档库中提取它，并验证最终重定位后的 load-config 指针等于该配置对象的虚拟地址。缺失、绝对、已丢弃、被截断或重定位错误的定义都会导致链接错误。

`/GUARD:MIXED` 为受保护对象文件和旧式对象文件的混合输入启用 CFG 输出。它会生成 5 字节的 GFID 和 GIAT 条目：4 字节 RVA 加 1 字节元数据；当前普通目标的元数据为零。其 `GuardFlags` 包含 CFG 和条目大小位。旧式对象通过保守扫描重定位来提供地址被获取目标，同时排除展开元数据。
当 `/GUARD:MIXED` 与 `/GUARD:EHCONT` 组合使用时，EH 延续目标表也使用 5 字节条目：4 字节 RVA 后跟 1 个值为零的元数据字节。

显式的增量链接请求与 `/ENCLAVE` 不兼容，因此会被拒绝。链接器采用最后一个生效的 `/INCREMENTAL` 选项，包括来自对象文件指令的选项。

`/ENCLAVE` 不会隐式选择 DLL 输出、CFG、完整性检查、enclave CRT 库、VEIID 处理或签名。请在构建流水线中显式指定这些选择。在捆绑运行时模式下，只有显式指定全局 `/NODEFAULTLIB` 时，才会启用上文所述的 enclave CRT/UCRT 搜索路径和五库校验；没有该选项时，仍使用捆绑的普通 Windows 运行时路径。显式用户工具链覆盖项继续保持通常的优先级。

## 构建和部署流程

1. 在启用 CFG 的情况下编译安全敏感源文件，例如使用 `-fms-guard=cf`。当最终链接使用 `/GUARD:MIXED` 时，旧式对象可以保持未插桩状态。
2. 定义 enclave 配置和入口点，然后链接 enclave CRT/UCRT 以及所需的 Vertdll 和 BCrypt 导入库。
3. 检查未签名的 PE 映像，并验证其 load-config 目录、CFG 表、enclave 配置指针和基址重定位。
4. 在 Windows 上对完成的映像运行 Windows SDK 的 VEIID 工具。
5. 在 Windows 上使用 SignTool 对经过 VEIID 处理的映像签名。签名必须是最后一次文件修改。
6. 在 Windows 宿主程序中检查 `IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS)`，使用 `CreateEnclave` 分配 enclave，通过 `LoadEnclaveImage` 加载 DLL，并调用 `InitializeEnclave`。
7. 使用 `GetProcAddress` 解析 enclave 导出函数，通过 `CallEnclave` 进入该函数并验证返回值。重复调用，覆盖受 CFG 保护和旧式的间接调用路径。
8. 使用 `TerminateEnclave` 和 `DeleteEnclave` 终止并释放 enclave，检查两个操作均成功。

对于反作弊系统，enclave 适合承载小型验证或密钥处理组件；这类组件的代码和私有状态需要与普通游戏进程之间建立更强的边界。请保持 enclave 接口精简，并验证宿主提供的所有数据：宿主仍然控制输入、调度、存储和可用性。VBS enclave 是对服务器端权威、遥测、驱动程序防御和常规进程加固的补充，而不是替代。

## 验证

`VBS enclave differential CI` 工作流在 Windows 上运行。其静态门禁会：

- 构建 NeverC 链接器和聚焦的 COFF 测试；
- 创建等价的 Microsoft 链接和 NeverC 链接 enclave DLL；
- 比较公开的 PE/load-config/CFG 语义；
- 对 PE 验证器运行变异测试；以及
- 为差分运行时探测准备经过 VEIID 处理的映像。

x64 运行时探测会先执行 Microsoft 映像，再执行两个 NeverC 候选映像。它使用 `GetProcAddress` 解析导出函数，通过 `CallEnclave` 重复调用，覆盖受 CFG 保护和旧式的间接调用路径，并检查返回值。`PASS` 要求完整且顺序正确的生命周期阶段证据，包括成功终止和释放；仅加载和初始化成功并不够。ARM64 的覆盖范围是静态验证和差分链接，不包含实际运行。

可选的运行时探测仅能在函数执行前，因已识别的环境准备条件不可用而报告 `SKIP`。崩溃、超时、阶段证据缺失或乱序以及功能错误均为 `FAIL`，Microsoft 参考映像也不例外。配置好的自托管 VBS runner 可以将运行时成功设为强制门禁。

链接器支持 x86-64 和 ARM64 COFF enclave 映像。它会验证已发布的配置指针，然后根据最终的普通 DLL 导入集合生成连续的 80 字节 `IMAGE_ENCLAVE_IMPORT` 条目。条目初始只包含导入名称，标识字段均为零，供 VEIID 绑定；链接器会回填数量、列表和条目大小。活动的延迟加载导入会被拒绝。链接器不会对 `IMAGE_ENCLAVE_CONFIG` 内部带版本的字段施加额外策略。
