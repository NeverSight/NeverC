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
| Android 内核 `.ko`（ELF ET_REL） | `.debug*`、`.comment`、重定位不需要的局部/未定义项，以及普通保留定义的可读名称 | 一个链接到 `.strtab` 的 `.symtab`、全部重定位及目标、精确的加载器/CFI 名称、精确导入、受保护段内名称与模块 ABI 元数据 |
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

`neverc make release` 仍是推荐的发布命令，并展开为 `-O2 --strip`。没有
`.nvk-build-flags` 时，`make` 默认使用 debug，不会自行选择 release。示例
Makefile 会保存显式选择的 profile，因此后续 `make push`、`make run` 与不带
目标的 `make` 会继续使用同一产物。接受 `EXTRA` 的示例会在递归构建和后续
构建中完整保留其多词值。`make debug` 或显式 `PROFILE=...` 会替换
保存的选择；`make clean` 会删除状态，使下一次构建恢复为 debug。显式执行
`make release` 会无条件重建一次模块/映射输出包，因此再次执行即可修复映射缺失
或摘要不匹配的输出包。在最终路径中，NeverC 会删除调试段、`.comment` 以及
重定位不需要的局部/未定义项，然后重建 `.strtab`。

发布成功后，NeverC 会以事务方式发布模块及其旁边的
`<module>.ko.symbols.json`。旧文件会一直保留到各自发生原子替换，普通进程错误
指向同一输出目录的并发发布会通过 `.neverc-output.lock` 串行执行；示例的
`make clean` 会有意保留这个内部锁文件，以免解除正在使用的锁。普通进程错误
在发布前会回滚整个输出包；较晚发生的持久性错误会保留恢复日志。由于两个目录项
无法通过一次文件系统操作
同时替换，异常关机后仍应校验 `image_sha256`。映射记录每个仍保留但已改名符号的
`original`（原名）与 `release`（`.ko` 中的名称）：

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 2,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

条目按 `release` 排序。被删除的符号和必须原样保留的加载器、导入、CFI 名称不会
写入，因为它们不需要翻译。若 debug 或其他非 strip 构建覆盖同一路径，NeverC
会删除旧映射，防止把旧副产物误用于新模块。ELF 允许符号名包含非 UTF-8 字节；
这种少见的原名会以 Base64 写入 `original`，并带有
`"original_encoding": "base64"`。其余原名保持可读。NeverC 在 POSIX 上以
`0600` 模式发布副产物，在 Windows 上使用受保护且仅允许所有者访问的
`Windows ACL`；若无法应用该限制，发布会失败。映射应作为私有调试产物归档，
不要随 `.ko` 发布或推送到设备。定位崩溃日志前，应先校验映射确实属于当前
`.ko`，再查询发布名称：

```bash
actual="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" &&
expected="$(jq -er '.image_sha256 | strings | select(test("^[0-9a-f]{64}$"))' \
  nvk_hello.ko.symbols.json)" &&
test "$actual" = "$expected" &&

python3 - nvk_hello.ko.symbols.json fn_C000 <<'PY'
import base64, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    entry = next(item for item in json.load(stream)["symbols"]
                 if item["release"] == sys.argv[2])
original = entry["original"]
print(repr(base64.b64decode(original))
      if entry.get("original_encoding") == "base64" else original)
PY
```

符合条件且被保留的定义会获得确定性的、受 IDA 启发但不占用保留前缀的结构名：

- `STT_FUNC` 使用 `fn_HEX`；
- `STT_OBJECT` 使用 `obj_HEX`；
- 位于可执行段的 `STT_NOTYPE` 使用 `code_HEX`；
- 其他已分配的 `STT_NOTYPE` 使用 `sym_HEX`；
- `SHN_ABS` 使用 `abs_HEX`；
- 不在 `SHF_ALLOC` 段中的定义使用
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`。

所有 `HEX` 字段（包括非分配形式中的两个字段）均使用大写十六进制且不补
无意义的前导零；多个符号需要同一名称时，会按确定性顺序追加十进制别名
`_1`、`_2` 等。

这些名称借鉴 IDA 的表达方式，但不占用它的 dummy-name 命名空间。实测在全新的
IDA 9.4 数据库中，ELF 用户符号 `sub_0`、`sub_4`、`loc_8` 会显示为
`_sub_0`、`_sub_4`、`_loc_8`，而 `fn_0`、`code_8`、`obj_10` 会原样显示。
Hex-Rays 的 [`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html)
文档也说明，以 `sub_` 等 dummy 前缀开头的用户名称会自动补 `_`。NeverC 不会
故意清空普通定义的 `st_name` 来诱导 IDA 生成 `sub_`：Android/Linux 模块的
kallsyms 长期忽略零名称项，空名称也会破坏可审计的序列化命名合约。原本就必须
为空的条目和段符号仍保持原样。

ELF 允许多个符号共享同一个 canonical analysis EA。NeverC 会在 `.symtab` 中
保留或生成完整的别名集合；但 IDA 9.4 的地址命名模型可能只物化同址符号中的
一个主名称。因此，IDA 未显示某个别名并不表示它已从 ELF 丢失；完整集合应使用
`llvm-readelf` 或 `llvm-nm` 审计。

对于已分配符号，`HEX` 是 NeverC canonical analysis EA，即仅用于静态分析的
规范有效地址。计算从游标 0 开始，按最终段表顺序遍历最终保留的 `SHF_ALLOC`
段：先按 `max(sh_addralign, 1)` 对齐游标并记为该段基址，再累加
`max(sh_size, 1)`；EA 等于该基址加最终 `st_value`。`abs_HEX` 直接使用最终
绝对 `st_value`。在非分配形式中，`FINAL_SECTION_ORDINAL_HEX` 是最终段序号，
`OFFSET_HEX` 是该段内的最终 `st_value`。这些坐标不是哈希、加密结果、文件
偏移、ELF 虚拟地址或内核运行时地址；加载器与 KASLR 可能在运行时重新布局
模块。

以下名称保持完全不变：

- 所有 `SHN_UNDEF` 导入，因为模块加载器按名称解析它们；
- 定义在 `.modinfo`、`.text.ftrace_trampoline`、
  `.gnu.linkonce.this_module`、`__versions` 或 `.codetag.alloc_tags` 中的符号；
- `init_module`、`cleanup_module`、`__cfi_check`、`__cfi_check_fail`、
  `__cfi_jt_init_module` 与 `__cfi_jt_cleanup_module`；
- 以 `__typeid__` 或 `__kcfi_typeid_` 开头的名称。

IDA 显示的 `extern` 区域只是分析器合成的视图，并不是真实 ELF 段。在最终
`ET_REL` `.ko` 中，外部重定位目标是 `.symtab` 内的 `SHN_UNDEF` 项，加载器需要
其原名。策略因此依据真实的 ELF 符号类别与定义段：未定义导入保持原名，符合
条件的定义则会改名，不受分析工具如何分组影响。

所有名称都会在修改前进行全局规划。共享同一基础候选名的定义会按确定性顺序
依次获得无编号形式、`_1`、`_2` 等；这种正常的名称分配不是错误。生成名称与
必须原样保留名称的保留命名空间冲突，或坐标/编号运算超出数值范围时，发布
收尾才会失败。遇到 `SHN_COMMON`、`SHN_LIVEPATCH` 或未知的 ELF 保留段索引时，
发布收尾也会保守拒绝，而不是猜测处理方式。可加载的最终模块不应包含
`SHN_COMMON`，请用 `-fno-common` 编译。Livepatch 模块要求保留原始符号表
顺序、索引及额外重定位元数据，本发布策略不宣称支持这些要求。

识别会使用多重信号：任一 `SHN_LIVEPATCH` 符号、`.klp.*` 段、
`SHF_RELA_LIVEPATCH` 标志，或按 NUL 分隔且以 `livepatch=` 开头的 `.modinfo`
字段，都会把产物判定为 livepatch 模块并保守拒绝。即使不存在任何 `.klp.*`
段或 livepatch 重定位标志，仅有该 `.modinfo` 标记也足以拒绝产物。

只有符合条件的 `.symtab` 名称会被替换。可加载的 `.ko` 仍然必须保留
`.symtab`、它关联的 `.strtab` 与重定位，因此通用工具将其显示为
`not stripped` 可能完全正常。BTF、模块导出、`.modinfo`、`__versions`、
trace 元数据、`__ksymtab_strings`、`.rodata` 与字符串字面量等独立存储或
接口仍可能泄露原名或其他识别文本。普通内核符号名在 kallsyms 与诊断中也会
变化，因此按符号使用 ftrace、kprobe/BPF，以及阅读崩溃报告都会变得不便。
诊断时请使用未剥离的 debug 构建，发布模块也不得依赖私有符号的原始名称。

### 最终 Android 发布的插件边界

发布收尾会在插件输出阶段两侧建立两个相互独立、失败即关闭的身份边界：

- 在任何可替换的 `ObjectGraph` 阶段之前，图身份封印会绑定每个保留逻辑段的
  `section ID`、`final ordinal` 与精确名称；还会把每个必须保持原名的符号的
  `symbol ID` 绑定到其名称、类别、定义段、值、大小、绑定、类型与完整
  `st_other`。发布验证器会另外重新计算普通结构名。
- 宿主建立可信写出基线之后、`neverc.object.post_write` 之前，映像身份封印会
  绑定每个保留逻辑段的序号/名称、`.symtab` 总项数，并把每个精确名称符号的
  名称及属性绑定到原始 `.symtab` `slot`。

因此能力矩阵被刻意限制为：

| 阶段绑定 | 最终 Android 发布行为 |
|----------|-----------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED`；在它能够替换宿主建立的可信写出基线之前拒绝 |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`；最终 Android 发布必须使用负责建立可信基线的宿主自有 graph writer |
| `observer` | `READ_ONLY`；允许观察，但不能修改产物 |
| `neverc.object.post_write` `interceptor` | `VALIDATED`；只能修改不属于身份面的 payload 字节，而且结果必须继续通过发布验证器、输入 ABI 合约与两层身份封印 |

最终合并的所有权同样由宿主封闭。来自 `third-party ObjectMergeProvider` 的
`MergedImage` 或独立字节会被丢弃，由 `host-owned graph writer` 序列化该 provider
已验证并完成收尾的图。反向一侧，`built-in finalized input serialization` 会绕过
`external object phases`，把完全一致的 `audited native bytes` 交给宿主 merger；
这一内部输入步骤不会绕过上述输出边界。

Finalization 只在 `Android module merge semantics` 下接受；
`relocatable output request` 与 `relocatable driver configuration` 也必须同时成立，
否则会在 `before routing` 失败。对于最终 Android relocatable 发布，
`frozen input format`、
`TargetKey.ObjectFormatID` 与 `frozen output format` 必须共享
`one format identity`。不一致会在 `before provider dispatch` 被拒绝——这也早于
route planning 与 sink creation——因此能力预检和实际 graph-writer dispatch
不可能看到不同格式。

对于 ObjectGraph 能完整表达的普通输入，较早的图 interceptor 只有在同时保持
图封印与全部发布语义时才能运行。如果输入包含 `ObjectGraph` 无法表达、必须靠
原生映像透传的事实，则所有可替换的 `route-matching provider` 与所有 interceptor
都会被拒绝；target/CPU/features/object-format/execution-level route 不匹配的 provider
既不运行，也不阻止发布。只允许只读 observer。只有发生在
`before sealed commit` 的拒绝或验证失败才会中止 staging
且不发布文件；`AFTER_COMMIT` observer 的失败发生在发布之后，只会被报告，
不能回滚已经发布的文件。

不要再用 `llvm-strip --strip-all` 或 `objcopy` 后处理 `.ko`，也不要盲目删除
codetag/BTF/ABI 段。如需模块签名，必须先剥离，再对最终字节签名，因为签名后
的任何修改都会使签名失效。`clean` 只能删除文件，绝不能剥离或签名现有模块。

## 安全边界

剥离会删除价值较高的命名与调试元数据，从而提高分析成本，但无法让原生
机器码变得不可逆向。正确剥离的二进制仍可能包含：

- 加载器必需的动态导入与导出名称；
- `.ko` 中加载器必需的名称，以及存放在 `.symtab` 之外的名称；
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
下方取反的 `strings` 检查预期没有任何匹配，并且只有此时才成功退出。

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

对于可加载的 ELF `ET_REL` `.ko`，通用 `file` 工具仍可能显示
`not stripped`，因为 `.symtab` 是有意保留的。不要用该标签判断 release
成败；应检查 DWARF 与 `.comment` 已消失，符合条件的定义使用规范的
`fn_`/`obj_`/`code_`/`sym_`/`abs_` 大写十六进制形式，
`SHN_UNDEF` 导入及必需的加载器/CFI 名称保持不变，且重定位有效。如需控制
名称泄露，还应分别审计 BTF、导出、modinfo、versions、trace 元数据与字符串。

剥离后的产物不应包含源码级调试段或私有静态符号名。必需的动态名称和
运行时元数据属于预期内容，不应被判定为剥离失败。
