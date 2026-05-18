<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← مُجمِّع shellcode](../README.ar.md)

# دليل تجميع ARM64 (AArch64) — منظور Shellcode

> للقراء غير المألوفين مع ARM64، مع التركيز على التعليمات التي يولدها مُجمِّع shellcode.

## 1–7. السجلات، التفرعات، العنونة النسبية لـ PC، تحميل الفوريات، الوصول للذاكرة، الحساب، المقارنات

سجلات عامة x0-x30 (64-بت) / w0-w30 (32-بت)، sp، xzr/wzr. اتفاقية AAPCS64. `b`/`bl`/`br`/`blr`/`ret`. **يجب على shellcode تجنب `bl`** (إعادة تحديد موقع BRANCH26). `adr`/`adrp+add`. `mov+movk` لـ 64-بت — **جوهر Data2TextPass**.

## 8. تسلسلات التعليمات النموذجية المولدة

حساب صرف، فيبوناتشي تكراري، تضمين سلسلة في المكدس (Data2TextPass)، استدعاء نظام (SyscallStubPass svc مباشر). تدفق التعليمات 100% في `__TEXT,__text`.

## 9. ملخص رئيسي

| المفهوم | x86_64 | ARM64 | Shellcode |
|---------|--------|-------|-----------|
| استدعاء دالة | `call rel32` | `bl imm26` | المستخرج يرقع BRANCH26 |
| تحميل عنوان | `lea rax,[rip+sym]` | `adrp+add` | PAGE21/PAGEOFF12 مرقعة |
| فورية 64-بت | `mov rax,imm64` | `mov+movk ×4` | صفر إعادة تحديد مواقع |
| استدعاء نظام | `syscall` | `svc #0x80` | Darwin: x16=nr |

</div>
