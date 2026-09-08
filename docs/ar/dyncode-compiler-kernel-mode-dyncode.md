<div dir="rtl">

**اللغات**: [English](../dyncode-compiler-kernel-mode-dyncode.md) | [简体中文](../zh-CN/dyncode-compiler-kernel-mode-dyncode.md) | [繁體中文](../zh-TW/dyncode-compiler-kernel-mode-dyncode.md) | [日本語](../ja/dyncode-compiler-kernel-mode-dyncode.md) | [한국어](../ko/dyncode-compiler-kernel-mode-dyncode.md) | [Français](../fr/dyncode-compiler-kernel-mode-dyncode.md) | [Deutsch](../de/dyncode-compiler-kernel-mode-dyncode.md) | [Español](../es/dyncode-compiler-kernel-mode-dyncode.md) | [Italiano](../it/dyncode-compiler-kernel-mode-dyncode.md) | [Русский](../ru/dyncode-compiler-kernel-mode-dyncode.md) | [العربية](dyncode-compiler-kernel-mode-dyncode.md)

[← مُجمِّع dyncode](dyncode-compiler.md)

# دعم dyncode وضع النواة (Ring-0)

`-fdyncode` غطى في الأصل حمولات ring-3 فقط. حمولات ring-0 لا يمكنها إعادة استخدام ABI ring-3: لا يوجد TEB/PEB، تعليمات syscall هي أفخاخ مستخدم→نواة، x86_64 يحتاج نموذج كود مختلف وتعطيل المنطقة الحمراء.

## 1. `-mdyncode-context={user,kernel}`
- **User** (افتراضي): مسار PEB/syscall.
- **Kernel**: SyscallStub/WinPEB معطلان، رايات النواة محقونة، KernelImportPass مفعّل.

## 2–3. حقول TargetDesc ورايات المشغل
`Level`، `KernelImport`، `KernelInjectFlags`. x86_64: `-mno-red-zone -mcmodel=kernel -mno-sse`؛ AArch64: `-mgeneral-regs-only`.

## 4. KernelImportPass
إعادة كتابة تلقائية لاستدعاءات extern غير محلولة إلى استدعاءات غير مباشرة عبر المُحلل. تجزئة FNV-1a 64-بت. دفاع ثلاثي الطبقات.

## 5–7. نواة Android، ملفات الرأس، كتابة كود Ring-0
`<neverc/dyncode/kernel.h>` يوفر `neverc_kern_resolve_t` و`neverc_kern_hash()`. حمولات حساب صرف أو قائمة على المُحلل.

## 8. خارطة الطريق
تبديل سياق النواة، إعادة كتابة المُحلل، نوعا الحمولة — كلها مكتملة. ملفات رأس SDK النواة مخططة.

</div>
