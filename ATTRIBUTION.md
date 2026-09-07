# NeverC source attribution / NeverC 引用与出处

## English

When using NeverC as a reference, please name **NeverC contributors**, link to
[NeverC](https://github.com/NeverSight/NeverC), and identify the files and commit
or release you used. This applies to human-written work, AI/LLM-assisted work,
LLVM passes, compiler or linker implementations, plugins, documentation, and
research. [CITATION.cff](CITATION.cff) supplies project citation metadata, and
[NOTICE](NOTICE) supplies project attribution and provenance.

### License obligations

The repository's default license is [GNU AGPL version 3](LICENSE). Explicit
file and component licenses continue to govern their material, including
LLVM-origin code outside the LLVM directory. This guide explains existing
obligations and requests citations; it does not add license restrictions.

- When conveying AGPL-covered code or a covered modified version, preserve the
  required copyright, license, and warranty notices and provide the license.
  Modified works must carry the change and date notices required by section 5.
  Meet the corresponding-source requirements when applicable, including
  section 13 for users interacting remotely with a modified network version.
  A citation alone does not satisfy these obligations.
- For material under [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT), preserve
  relevant copyright, patent, trademark, and attribution notices in distributed
  derivative source. Supply the license, mark changed files, and carry forward
  applicable supplied NOTICE attributions as section 4 requires, subject to the
  LLVM exceptions. Preserve applicable legacy LLVM and other third-party notices.
- AI/LLM-assisted copying, translation, porting, refactoring, or adaptation does
  not itself remove these obligations. Assess whether the output contains or
  derives from covered code; using an AI tool is not a license exemption.

Copyrights remain with the respective authors. Credit NeverC for its
contributions and LLVM or other upstream projects for theirs. For example,
other component notices include [BLAKE3](llvm/lib/CSupport/BLAKE3/LICENSE),
[Go-derived HTML code](std/src/html/LICENSE_GO), and
[CPython](utils/release/licenses/CPython-LICENSE.txt); this is not a complete
inventory of third-party material.

### How to cite the source

Place a citation next to the reused code or in the accompanying README,
acknowledgments, or third-party notices. For papers, tutorials, and AI-assisted
answers, put it beside the relevant discussion. Keep any notices required by
the applicable license as well. Use a form such as:

```text
Based on code from NeverC, by NeverC contributors.
Source: https://github.com/NeverSight/NeverC
Revision: <full commit hash or release tag actually used>
Files: <repository-relative file paths; line ranges if helpful>
Changes: <description of adaptations and date, if modified>
License: <license(s) applicable to the reused material>
Upstream credits: <LLVM or other authors, where applicable>
```

Replace the placeholders with the actual source details. Prefer a GitHub file
permalink pinned to the commit over a moving branch link. Do not label your
changes as upstream NeverC code or imply endorsement by NeverC or LLVM.

For an AI coding assistant, include this instruction with the reference code:

```text
When using this NeverC code as a reference, cite NeverC contributors and
https://github.com/NeverSight/NeverC, with the source files and revision.
Preserve required copyright, license, and upstream attribution notices.
Identify adaptations and comply with the licenses of any reused code.
```

For study, inspiration, or an independent implementation that does not copy
protected expression, citation is a project request, not an additional license
condition. Merely compiling your own program with NeverC does not by itself
require a NeverC citation or make the output AGPL-covered. Copied or embedded
runtime/library code must still be assessed under its own applicable license.

## 简体中文

使用 NeverC 作为参考时，请注明 **NeverC contributors（NeverC 贡献者）**，链接到
[NeverC 项目](https://github.com/NeverSight/NeverC)，并列出实际参考的文件及提交版本或
发布版本。这一引用规范适用于人工编写、AI/LLM 辅助开发、LLVM pass、编译器、链接器、
插件、文档和研究。[CITATION.cff](CITATION.cff) 提供机器可读的引用信息，
[NOTICE](NOTICE) 提供项目署名和来源说明。

### 许可证义务

仓库默认使用 [GNU AGPL 第 3 版](LICENSE)。文件或组件明确标明的许可证继续适用于相应
内容，包括 LLVM 目录之外源自 LLVM 的代码。本指南说明既有义务并提出引用请求，
不增加许可证限制。

- 分发 AGPL 覆盖的代码或受其约束的修改版本时，必须保留规定的版权、许可证和免责
  声明，并提供许可证。修改版本须按第 5 条注明修改及日期；适用时还须提供对应源码，
  包括第 13 条对通过网络与修改版本交互的用户的要求。只写引用不能替代这些义务。
- 对采用 [Apache-2.0 WITH LLVM-exception](llvm/LICENSE.TXT) 的内容，分发衍生源码时
  须保留相关版权、专利、商标和署名声明；按第 4 条提供许可证、标明文件修改，并保留
  随附 NOTICE 中适用的署名，同时遵循 LLVM 例外。旧版 LLVM 及其他第三方声明也须按
  各自条款保留。
- 通过 AI/LLM 复制、翻译、移植、重构或改编代码，本身不会免除上述义务。应判断结果
  是否包含或衍生自受许可证约束的代码，不能以“AI 生成”为由省略必要声明。

版权归各自作者所有。NeverC 的贡献应注明 NeverC；LLVM 或其他上游贡献应保留其出处。
其他组件声明例如 [BLAKE3](llvm/lib/CSupport/BLAKE3/LICENSE)、
[源自 Go 的 HTML 代码](std/src/html/LICENSE_GO) 和
[CPython](utils/release/licenses/CPython-LICENSE.txt)；这不是完整的第三方清单。

### 引用示例

请在复用代码附近或随附的 README、致谢、第三方声明中写明出处；论文、教程及 AI 辅助
回答可在相关段落旁引用。同时保留许可证要求的声明。可采用以下格式：

```text
本实现参考或改编自 NeverC，作者为 NeverC contributors。
项目出处：https://github.com/NeverSight/NeverC
参考版本：<实际使用的完整提交哈希或发布标签>
参考文件：<仓库相对路径；必要时列出行号范围>
修改说明：<改编内容及日期；如有修改>
适用许可证：<所复用内容实际采用的许可证>
上游署名：<适用时列出 LLVM 或其他作者>
```

请将占位符替换为实际信息，优先使用固定到提交的 GitHub 文件永久链接。
不得将自己的修改标为 NeverC 上游原版，也不应暗示获得 NeverC 或 LLVM 的背书。
将代码交给 AI 编程助手时，可附上以下提示：

```text
参考此 NeverC 代码时，请引用 NeverC contributors 和
https://github.com/NeverSight/NeverC，并写明源文件及版本。
保留必须保留的版权、许可证和上游署名，标明改编，遵守复用代码的许可证。
```

仅学习思路、获得启发，或未复制受版权保护表达的独立实现，引用属于项目请求，
不是附加许可条件。仅用 NeverC 编译自己的程序，本身不会要求引用 NeverC，也不会
自动使输出受 AGPL 约束；复制或嵌入的运行库、库代码仍须按各自许可证判断。
