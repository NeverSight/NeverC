**Languages**: [English](attribution.md) | [简体中文](zh-CN/attribution.md) | [繁體中文](zh-TW/attribution.md) | [日本語](ja/attribution.md) | [한국어](ko/attribution.md) | [Français](fr/attribution.md) | [Deutsch](de/attribution.md) | [Español](es/attribution.md) | [Italiano](it/attribution.md) | [Русский](ru/attribution.md) | [العربية](ar/attribution.md)

[← Documentation index](README.md) · [← NeverC project](../README.md)

# NeverC source attribution

When using NeverC as a reference, please name **NeverC contributors**, link to
[NeverC](https://github.com/NeverSight/NeverC), and identify the files and commit
or release you used. This applies to human-written work, AI/LLM-assisted work,
LLVM passes, compiler or linker implementations, plugins, documentation, and
research. [CITATION.cff](../CITATION.cff) supplies project citation metadata, and
[NOTICE](../NOTICE) supplies project attribution and provenance.

## License obligations

The repository's default license is [GNU AGPL version 3](../LICENSE). Explicit
file and component licenses continue to govern their material, including
LLVM-origin code outside the LLVM directory. This guide explains existing
obligations and requests citations; it does not add license restrictions.

- When conveying AGPL-covered code or a covered modified version, preserve the
  required copyright, license, and warranty notices and provide the license.
  Modified works must carry the change and date notices required by section 5.
  Meet the corresponding-source requirements when applicable, including
  section 13 for users interacting remotely with a modified network version.
  A citation alone does not satisfy these obligations.
- For material under [Apache-2.0 WITH LLVM-exception](../llvm/LICENSE.TXT), preserve
  relevant copyright, patent, trademark, and attribution notices in distributed
  derivative source. Supply the license, mark changed files, and carry forward
  applicable supplied NOTICE attributions as section 4 requires, subject to the
  LLVM exceptions. Preserve applicable legacy LLVM and other third-party notices.
- AI/LLM-assisted copying, translation, porting, refactoring, or adaptation does
  not itself remove these obligations. Assess whether the output contains or
  derives from covered code; using an AI tool is not a license exemption.

Copyrights remain with the respective authors. Credit NeverC for its
contributions and LLVM or other upstream projects for theirs. For example,
other component notices include [BLAKE3](../llvm/lib/CSupport/BLAKE3/LICENSE),
[Go-derived HTML code](../std/src/html/LICENSE_GO), and
[CPython](../utils/release/licenses/CPython-LICENSE.txt); this is not a complete
inventory of third-party material.

## How to cite the source

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
