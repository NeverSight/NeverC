---
name: ctf-reverse
description: Reverse compiled, packed, obfuscated, virtualized, or anti-analysis targets for an authorized CTF challenge. Use while understanding the target; switch to an exploitation skill once the vulnerability is known.
license: MIT
metadata:
  user-invocable: "false"
---

# CTF reverse engineering

Identify the target format and architecture, map the validation or decoding path, and automate only after the relevant behavior is understood. Treat challenge files as untrusted: prefer isolated analysis and avoid installing tools or running unknown binaries unless the task and environment permit it.

## Topic routing

Read only the material relevant to the target:

- [Static tools](tools.md), [dynamic tools](tools-dynamic.md), and [emulation](tools-emulation.md) for initial tool selection.
- [Advanced tools I](tools-advanced.md) and [advanced tools II](tools-advanced-2.md) for symbolic execution, deobfuscation, patching, or VM-heavy work.
- [Anti-analysis taxonomy](anti-analysis.md) and [CTF anti-analysis cases](anti-analysis-ctf.md) when debugging, timing, integrity, or sandbox checks block progress.
- [Core patterns](patterns.md), [runtime patterns](patterns-runtime.md), and [CTF pattern sets I](patterns-ctf.md), [II](patterns-ctf-2.md), and [III](patterns-ctf-3.md) after the first-pass control-flow map identifies the relevant family.
- [Language patterns](languages.md), [language platforms](languages-platforms.md), and [compiled languages](languages-compiled.md) for runtime-specific artifacts.
- [Platform notes](platforms.md) and [hardware or architecture notes](platforms-hardware.md) for OS, firmware, mobile, kernel, and ISA-specific work.
- [Field notes](field-notes.md) for a compact lookup after target classification.

## Workflow

Start with file metadata, strings, imports, entry points, and obvious comparisons. Use tracing or breakpoints to confirm behavior before patching. Recover the transformation or acceptance condition, build the smallest solver or extractor that expresses it, and validate the result against the original challenge.

Pivot when reversing is no longer the blocker: exploitation, web, forensics, cryptography, malware analysis, or a pure encoding puzzle should use a skill dedicated to that task when one is available.
