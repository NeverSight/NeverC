<div dir="rtl">

**اللغات**: [English](../dyncode-compiler-arm64-assembly-tutorial.md) | [简体中文](../zh-CN/dyncode-compiler-arm64-assembly-tutorial.md) | [繁體中文](../zh-TW/dyncode-compiler-arm64-assembly-tutorial.md) | [日本語](../ja/dyncode-compiler-arm64-assembly-tutorial.md) | [한국어](../ko/dyncode-compiler-arm64-assembly-tutorial.md) | [Français](../fr/dyncode-compiler-arm64-assembly-tutorial.md) | [Deutsch](../de/dyncode-compiler-arm64-assembly-tutorial.md) | [Español](../es/dyncode-compiler-arm64-assembly-tutorial.md) | [Italiano](../it/dyncode-compiler-arm64-assembly-tutorial.md) | [Русский](../ru/dyncode-compiler-arm64-assembly-tutorial.md) | [العربية](dyncode-compiler-arm64-assembly-tutorial.md)

[← مُجمِّع dyncode](dyncode-compiler.md)

# دليل تجميع ARM64 (AArch64) — منظور DynCode

> للقراء غير المألوفين مع ARM64، مع التركيز على التعليمات التي يولدها مُجمِّع dyncode.

## 1–7. السجلات، التفرعات، العنونة النسبية لـ PC، تحميل الفوريات، الوصول للذاكرة، الحساب، المقارنات

سجلات عامة x0-x30 (64-بت) / w0-w30 (32-بت)، sp، xzr/wzr. اتفاقية AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **يجب على dyncode تجنب `bl`** (إعادة تحديد موقع BRANCH26). `adr`/`adrp+add`. `mov+movk` لـ 64-بت — **جوهر Data2TextPass**.

## 8. تسلسلات التعليمات النموذجية المولدة

حساب صرف، فيبوناتشي تكراري، تضمين سلسلة في المكدس (Data2TextPass)، استدعاء نظام (SyscallStubPass svc مباشر). تدفق التعليمات 100% في `__TEXT,__text`.

## 9. ملخص رئيسي

| المفهوم | x86_64 | ARM64 | DynCode |
|---------|--------|-------|-----------|
| استدعاء دالة | `call rel32` | `bl imm26` | المستخرج يرقع BRANCH26 |
| تحميل عنوان | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 مرقعة |
| فورية 64-بت | `mov rax,imm64` | `mov+movk ×4` | صفر إعادة تحديد مواقع |
| استدعاء نظام | `syscall` | `svc #0x80` | Darwin: x16=nr |

</div>
