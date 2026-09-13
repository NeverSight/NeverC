---
name: llvm-security
description: Apply or develop LLVM sanitizers, compiler hardening, and exploit mitigations. Use when secure compilation is central, not for general vulnerability analysis unrelated to the toolchain.
---

# LLVM security

Use this skill when the compiler or toolchain supplies the security property or diagnostic.

## Topic routing

- [Sanitizers and hardening](references/sanitizers-and-hardening.md) for ASan, UBSan, MSan, TSan, CFI, SafeStack, and hardening flags.
- [Analysis and mitigations](references/analysis-and-mitigations.md) for symbolic execution, security checks, and target-level mitigations.
- [Secure pipelines](references/secure-pipelines.md) for build policy, fuzzing integration, and Windows-specific protection.
- [Resources](references/resources.md) for upstream tools and documentation.

Match flags and mitigations to the actual target and deployment environment. Verify that the resulting binary carries or exercises the intended property; a successful compilation alone is insufficient evidence.
